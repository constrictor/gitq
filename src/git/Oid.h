// Value type for object ids, hashable for Qt containers.
#pragma once

#include <git2.h>
#if __has_include(<git2/sys/errors.h>)
#include <git2/sys/errors.h>   // git_error_clear moved here in libgit2 1.8
#endif

#include <QHash>
#include <QMetaType>
#include <QString>
#include <cstring>

namespace gitq {

struct Oid {
    git_oid oid{};

    Oid() = default;
    Oid(const git_oid &o) : oid(o) {}
    Oid(const git_oid *o) { if (o) oid = *o; }

    static Oid fromHex(const QString &hex) {
        Oid r;
        QByteArray b = hex.toLatin1();
        if (git_oid_fromstrn(&r.oid, b.constData(), size_t(b.size())) < 0) return Oid();
        return r;
    }

    // Pseudo ids used for "local uncommitted changes" rows, as in gitk.
    static Oid workdirId() { Oid r; return r; }          // 0000...0000
    static Oid indexId() { Oid r; r.oid.id[GIT_OID_SHA1_SIZE - 1] = 1; return r; } // 0000...0001

    bool isNull() const { return git_oid_is_zero(&oid); }
    bool isWorkdir() const { return isNull(); }
    bool isIndex() const { return *this == indexId(); }
    bool isPseudo() const { return isWorkdir() || isIndex(); }

    const git_oid *ptr() const { return &oid; }

    QString hex() const {
        char buf[GIT_OID_MAX_HEXSIZE + 1];
        git_oid_tostr(buf, sizeof buf, &oid);
        return QString::fromLatin1(buf);
    }
    QString shortHex(int n = 7) const { return hex().left(n); }

    friend bool operator==(const Oid &a, const Oid &b) { return git_oid_equal(&a.oid, &b.oid); }
    friend bool operator!=(const Oid &a, const Oid &b) { return !(a == b); }
    friend bool operator<(const Oid &a, const Oid &b) { return git_oid_cmp(&a.oid, &b.oid) < 0; }
};

inline size_t qHash(const Oid &o, size_t seed = 0) {
    size_t h;
    std::memcpy(&h, o.oid.id, sizeof h);
    return h ^ seed;
}

} // namespace gitq

Q_DECLARE_METATYPE(gitq::Oid)
