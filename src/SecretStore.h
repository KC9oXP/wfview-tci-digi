#pragma once

// Saved passwords and keys (QRZ logbook key, TQSL certificate password).
//
// At rest: Windows Credential Manager only (encrypted under the user's Windows login).
// Never in the settings file, the data folder or any other file. Where there is no
// supported store (macOS / Linux for now) a secret lives in memory for this session only.
//
// In use: SecretBuffer. A recalled secret goes into its own block of memory from the
// operating system that is
//   - locked, so Windows does not write it to the page file,
//   - encrypted while it waits (CryptProtectMemory, this process only), and decrypted only
//     inside reveal() for the moment it is used,
//   - wiped (SecureZeroMemory) and given back as soon as the buffer is destroyed.
// Copies the program must hand on (the HTTPS request body for QRZ, TQSL's command line)
// are wiped by the caller as soon as they have been handed over; copies made inside Qt or
// TQSL itself are outside this program's control.

#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>
#include <string>

class SecretBuffer
{
public:
    // Takes a copy of bytes (the caller wipes its own copy).
    SecretBuffer(const char *bytes, size_t len);
    ~SecretBuffer();
    SecretBuffer(const SecretBuffer &) = delete;
    SecretBuffer &operator=(const SecretBuffer &) = delete;

    size_t size() const
    {
        return m_len;
    }
    bool empty() const
    {
        return m_len == 0;
    }
    // Decrypts, calls use(bytes, len), encrypts again. Do not keep the pointer.
    void reveal(const std::function<void(const char *, size_t)> &use) const;
    // The last 4 characters, for "saved (ends ...2E2F)".
    QString tail4() const;

private:
    char *m_mem = nullptr;
    size_t m_cap = 0; // allocated (page multiple)
    size_t m_len = 0;
    bool m_locked = false;
};

// Wipes a QByteArray / QString the program made (best effort: Qt may have made others).
void wipe(QByteArray &b);
void wipe(QString &s);
void wipe(std::string &s);
// Zeroes the bytes of b even if Qt shares them with another copy (call only when that
// other copy is finished with, e.g. after the network reply).
void wipeShared(const QByteArray &b);

namespace SecretStore
{
// "qrz-key", "tqsl-password"
bool persistent(); // true: saved across restarts (Windows Credential Manager)
bool save(const QString &name, QString &value); // wipes value
std::unique_ptr<SecretBuffer> load(const QString &name); // nullptr: nothing saved
bool has(const QString &name);
void forget(const QString &name);
// For the end-to-end test only: keep secrets in memory, never touch Credential Manager.
void setTestMode(bool on);
} // namespace SecretStore
