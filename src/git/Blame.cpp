#include "git/Blame.h"

#include "git/DiffEngine.h"
#include "git/GitPtr.h"
#include "git/Repository.h"

#include <QHash>

namespace gitq::Blame {

BlameResult blameFile(const Repository &repo, const QString &path, const Oid &atIn) {
    BlameResult r;
    Oid at = atIn.isNull() || atIn.isPseudo() ? repo.resolveCommit(QStringLiteral("HEAD")) : atIn;
    git_blame_options opts = GIT_BLAME_OPTIONS_INIT;
    opts.newest_commit = at.oid;
    opts.flags = GIT_BLAME_TRACK_COPIES_SAME_FILE;
    git_blame *b = nullptr;
    QByteArray p = path.toUtf8();
    if (git_blame_file(&b, repo.raw(), p.constData(), &opts) < 0) {
        r.error = lastGitError(QStringLiteral("blame"));
        git_error_clear();
        return r;
    }
    BlamePtr blame(b);
    QByteArray contents = DiffEngine::fileContents(repo, at, path);
    QStringList text = QString::fromUtf8(contents).split(QLatin1Char('\n'));
    if (!text.isEmpty() && text.last().isEmpty()) text.removeLast();

    QHash<Oid, QString> summaries;
    for (int i = 0; i < text.size(); ++i) {
        BlameLine line;
        line.text = text[i];
        const git_blame_hunk *h = git_blame_get_hunk_byline(b, size_t(i + 1));
        if (h) {
            line.commit = Oid(h->final_commit_id);
            line.origPath = QString::fromUtf8(h->orig_path ? h->orig_path : p.constData());
            line.origLine = int(h->orig_start_line_number + (size_t(i + 1) - h->final_start_line_number));
            line.author = personFrom(h->final_signature);
            auto it = summaries.find(line.commit);
            if (it == summaries.end()) {
                CommitPtr c = repo.lookupCommit(line.commit);
                it = summaries.insert(line.commit, c ? QString::fromUtf8(git_commit_summary(c.get())) : QString());
            }
            line.summary = *it;
        }
        r.lines.append(line);
    }
    return r;
}

} // namespace gitq::Blame
