#include "SecretStore.h"

#include <QByteArray>
#include <QMap>
#include <cstring>

#ifdef Q_OS_WIN
#include <dpapi.h>
#include <wincred.h>
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
// Zeroing the compiler may not remove.
void secureZero(void *p, size_t n)
{
    if (!p || !n)
        return;
#ifdef Q_OS_WIN
    SecureZeroMemory(p, n);
#else
    volatile unsigned char *v = static_cast<volatile unsigned char *>(p);
    while (n--)
        *v++ = 0;
#endif
}

#ifdef Q_OS_WIN
constexpr size_t kBlock = CRYPTPROTECTMEMORY_BLOCK_SIZE; // 16
#else
constexpr size_t kBlock = 16;
#endif

size_t pageSize()
{
#ifdef Q_OS_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize ? si.dwPageSize : 4096;
#else
    const long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? size_t(p) : 4096;
#endif
}
} // namespace

SecretBuffer::SecretBuffer(const char *bytes, size_t len)
{
    m_len = len;
    // A multiple of the encryption block, rounded up to whole pages.
    size_t need = ((len + kBlock - 1) / kBlock) * kBlock;
    if (need == 0)
        need = kBlock;
    const size_t page = pageSize();
    m_cap = ((need + page - 1) / page) * page;
#ifdef Q_OS_WIN
    m_mem = static_cast<char *>(VirtualAlloc(nullptr, m_cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (m_mem)
        m_locked = VirtualLock(m_mem, m_cap) != 0; // kept out of the page file
#else
    void *p = mmap(nullptr, m_cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    m_mem = p == MAP_FAILED ? nullptr : static_cast<char *>(p);
    if (m_mem)
        m_locked = mlock(m_mem, m_cap) == 0;
#endif
    if (!m_mem)
    {
        m_len = 0;
        return;
    }
    std::memset(m_mem, 0, m_cap);
    if (len)
        std::memcpy(m_mem, bytes, len);
#ifdef Q_OS_WIN
    CryptProtectMemory(m_mem, DWORD(need), CRYPTPROTECTMEMORY_SAME_PROCESS); // encrypted while waiting
#endif
}

SecretBuffer::~SecretBuffer()
{
    if (!m_mem)
        return;
    secureZero(m_mem, m_cap);
#ifdef Q_OS_WIN
    if (m_locked)
        VirtualUnlock(m_mem, m_cap);
    VirtualFree(m_mem, 0, MEM_RELEASE);
#else
    if (m_locked)
        munlock(m_mem, m_cap);
    munmap(m_mem, m_cap);
#endif
}

void SecretBuffer::reveal(const std::function<void(const char *, size_t)> &use) const
{
    if (!m_mem)
    {
        use("", 0);
        return;
    }
#ifdef Q_OS_WIN
    size_t need = ((m_len + kBlock - 1) / kBlock) * kBlock;
    if (need == 0)
        need = kBlock;
    CryptUnprotectMemory(m_mem, DWORD(need), CRYPTPROTECTMEMORY_SAME_PROCESS);
    use(m_mem, m_len);
    CryptProtectMemory(m_mem, DWORD(need), CRYPTPROTECTMEMORY_SAME_PROCESS);
#else
    use(m_mem, m_len);
#endif
}

QString SecretBuffer::tail4() const
{
    QString t;
    reveal(
        [&](const char *p, size_t n)
        {
            const size_t k = n < 4 ? n : 4;
            t = QString::fromUtf8(p + n - k, int(k));
        });
    return t;
}

void wipe(QByteArray &b)
{
    if (!b.isEmpty())
    {
        b.detach();
        secureZero(b.data(), size_t(b.size()));
    }
    b.clear();
}

void wipe(QString &s)
{
    if (!s.isEmpty())
    {
        s.detach();
        secureZero(s.data(), size_t(s.size()) * sizeof(QChar));
    }
    s.clear();
}

void wipe(std::string &s)
{
    if (!s.empty())
        secureZero(&s[0], s.size());
    s.clear();
    s.shrink_to_fit();
}

void wipeShared(const QByteArray &b)
{
    if (!b.isEmpty())
        secureZero(const_cast<char *>(b.constData()), size_t(b.size()));
}

namespace SecretStore
{
namespace
{
bool g_test = false;
// Session-only secrets (no persistent store, or test mode).
QMap<QString, std::shared_ptr<SecretBuffer>> &session()
{
    static QMap<QString, std::shared_ptr<SecretBuffer>> m;
    return m;
}
#ifdef Q_OS_WIN
std::wstring target(const QString &name)
{
    return (QStringLiteral("wfview-tci-digi/") + name).toStdWString();
}
#endif
} // namespace

void setTestMode(bool on)
{
    g_test = on;
}

bool persistent()
{
#ifdef Q_OS_WIN
    return !g_test;
#else
    return false;
#endif
}

bool save(const QString &name, QString &value)
{
    QByteArray b = value.toUtf8();
    wipe(value);
    bool ok = false;
    if (persistent())
    {
#ifdef Q_OS_WIN
        std::wstring t = target(name);
        CREDENTIALW c = {};
        c.Type = CRED_TYPE_GENERIC;
        c.TargetName = const_cast<LPWSTR>(t.c_str());
        c.CredentialBlobSize = DWORD(b.size());
        c.CredentialBlob = reinterpret_cast<LPBYTE>(b.data());
        c.Persist = CRED_PERSIST_LOCAL_MACHINE; // this PC only, does not roam with the profile
        wchar_t user[] = L"wfview TCI Digi";
        c.UserName = user;
        ok = CredWriteW(&c, 0) != 0;
#endif
    }
    else
    {
        session()[name] = std::make_shared<SecretBuffer>(b.constData(), size_t(b.size()));
        ok = true;
    }
    wipe(b);
    return ok;
}

std::unique_ptr<SecretBuffer> load(const QString &name)
{
    if (persistent())
    {
#ifdef Q_OS_WIN
        PCREDENTIALW c = nullptr;
        if (!CredReadW(target(name).c_str(), CRED_TYPE_GENERIC, 0, &c) || !c)
            return nullptr;
        auto buf = std::make_unique<SecretBuffer>(reinterpret_cast<const char *>(c->CredentialBlob),
                                                  size_t(c->CredentialBlobSize));
        secureZero(c->CredentialBlob, c->CredentialBlobSize); // Windows' copy, before freeing it
        CredFree(c);
        return buf;
#endif
    }
    auto it = session().find(name);
    if (it == session().end() || !it.value())
        return nullptr;
    std::unique_ptr<SecretBuffer> out;
    it.value()->reveal([&](const char *p, size_t n) { out = std::make_unique<SecretBuffer>(p, n); });
    return out;
}

bool has(const QString &name)
{
    if (persistent())
    {
#ifdef Q_OS_WIN
        PCREDENTIALW c = nullptr;
        if (!CredReadW(target(name).c_str(), CRED_TYPE_GENERIC, 0, &c) || !c)
            return false;
        secureZero(c->CredentialBlob, c->CredentialBlobSize);
        CredFree(c);
        return true;
#endif
    }
    return session().contains(name);
}

void forget(const QString &name)
{
#ifdef Q_OS_WIN
    if (persistent())
        CredDeleteW(target(name).c_str(), CRED_TYPE_GENERIC, 0);
#endif
    session().remove(name);
}
} // namespace SecretStore
