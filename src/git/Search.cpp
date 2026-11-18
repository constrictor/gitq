#include "git/Search.h"

#include "git/GitPtr.h"
#include "git/LogLoader.h"
#include "git/Repository.h"

#include <QElapsedTimer>

namespace gitq {

namespace Search {

QRegularExpression regexFor(const FindSpec &spec) {
    QString pattern = spec.mode == FindSpec::Regexp ? spec.text : QRegularExpression::escape(spec.text);
    auto opts = spec.mode == FindSpec::Exact ? QRegularExpression::NoPatternOption
                                             : QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, opts);
}

bool matchesFields(const CommitInfo &c, const FindSpec &spec) {
    if (spec.text.isEmpty()) return false;
    QRegularExpression re = regexFor(spec);
    if (!re.isValid()) return false;
    auto m = [&](const QString &s) { return re.match(s).hasMatch(); };
    switch (spec.field) {
    case FindSpec::Headline: return m(c.subject);
    case FindSpec::Comments: return m(c.message());
    case FindSpec::Author: return m(c.author.nameEmail());
    case FindSpec::Committer: return m(c.committer.nameEmail());
    case FindSpec::AllFields:
        return m(c.message()) || m(c.author.nameEmail()) || m(c.committer.nameEmail());
    }
    return false;
}

QList<QPair<int, int>> matchRanges(const QString &text, const FindSpec &spec) {
    QList<QPair<int, int>> out;
    if (spec.text.isEmpty()) return out;
    QRegularExpression re = regexFor(spec);
    if (!re.isValid()) return out;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        if (m.capturedLength() == 0) break;
        out.append({int(m.capturedStart()), int(m.capturedLength())});
    }
    return out;
}

namespace {

TreePtr tree(const Repository &repo, const git_oid *commitId) {
    CommitPtr c = repo.lookupCommit(Oid(commitId));
    git_tree *t = nullptr;
    if (!c || git_commit_tree(&t, c.get()) < 0) {
        git_error_clear();
        return {};
    }
    return TreePtr(t);
}

int countOccurrences(const QByteArray &data, const FindSpec &spec, const QRegularExpression &re) {
    if (spec.mode == FindSpec::Exact) {
        QByteArray needle = spec.text.toUtf8();
        int count = 0, pos = 0;
        while ((pos = int(data.indexOf(needle, pos))) >= 0) {
            ++count;
            pos += int(qMax<qsizetype>(1, needle.size()));
        }
        return count;
    }
    int count = 0;
    auto it = re.globalMatch(QString::fromUtf8(data));
    while (it.hasNext()) {
        if (it.next().capturedLength() == 0) break;
        ++count;
    }
    return count;
}

QByteArray blobData(const Repository &repo, const git_diff_file &f) {
    if (git_oid_is_zero(&f.id)) return {};
    git_blob *b = nullptr;
    if (git_blob_lookup(&b, repo.raw(), &f.id) < 0) {
        git_error_clear();
        return {};
    }
    BlobPtr g(b);
    return QByteArray(static_cast<const char *>(git_blob_rawcontent(b)), int(git_blob_rawsize(b)));
}

} // namespace

bool matchesContent(const Repository &repo, const Oid &id, const FindSpec &spec) {
    if (id.isPseudo() || spec.text.isEmpty()) return false;
    CommitPtr c = repo.lookupCommit(id);
    if (!c) return false;
    unsigned np = git_commit_parentcount(c.get());
    if (np > 1) return false;   // like git diff-tree without -m: merges show no changes
    TreePtr t = tree(repo, git_commit_id(c.get()));
    TreePtr pt = np ? tree(repo, git_commit_parent_id(c.get(), 0)) : TreePtr();

    if (spec.type == FindSpec::TouchingPaths) {
        QStringList paths = spec.text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        PathFilter pf(repo.raw(), paths);
        return !pf.treeSame(t.get(), pt.get());
    }

    git_diff *d = nullptr;
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.context_lines = 0;
    if (git_diff_tree_to_tree(&d, repo.raw(), pt.get(), t.get(), &opts) < 0) {
        git_error_clear();
        return false;
    }
    DiffPtr diff(d);
    QRegularExpression re = regexFor(spec);
    if (!re.isValid()) return false;
    size_t n = git_diff_num_deltas(d);
    for (size_t i = 0; i < n; ++i) {
        const git_diff_delta *delta = git_diff_get_delta(d, i);
        if (spec.type == FindSpec::AddingRemoving) {
            QByteArray oldData = blobData(repo, delta->old_file);
            QByteArray newData = blobData(repo, delta->new_file);
            if (countOccurrences(oldData, spec, re) != countOccurrences(newData, spec, re)) return true;
        } else {   // ChangingLines (-G)
            git_patch *p = nullptr;
            if (git_patch_from_diff(&p, d, i) < 0 || !p) {
                git_error_clear();
                continue;
            }
            PatchPtr patch(p);
            size_t nh = git_patch_num_hunks(p);
            for (size_t h = 0; h < nh; ++h) {
                int nl = git_patch_num_lines_in_hunk(p, h);
                for (int l = 0; l < nl; ++l) {
                    const git_diff_line *line = nullptr;
                    if (git_patch_get_line_in_hunk(&line, p, h, size_t(l)) < 0) continue;
                    if (line->origin != GIT_DIFF_LINE_ADDITION && line->origin != GIT_DIFF_LINE_DELETION) continue;
                    if (re.match(QString::fromUtf8(line->content, int(line->content_len))).hasMatch()) return true;
                }
            }
        }
    }
    return false;
}

} // namespace Search

SearchWorker::SearchWorker(QObject *parent) : QObject(parent) {}

SearchWorker::~SearchWorker() {
    cancel();
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void SearchWorker::cancel() {
    if (m_cancel) m_cancel->store(true);
}

void SearchWorker::start(const QString &repoPath, const QList<Oid> &commits, const FindSpec &spec) {
    cancel();
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto flag = m_cancel;
    const int gen = ++m_generation;
    SearchWorker *self = this;
    m_thread = QThread::create([=] {
        try {
            Repository repo = Repository::open(repoPath);
            QList<Oid> batch;
            QElapsedTimer timer;
            timer.start();
            auto flush = [&](int done) {
                QList<Oid> b = std::move(batch);
                batch = {};
                QMetaObject::invokeMethod(self, [self, gen, b, done, total = int(commits.size())] {
                    if (self->m_generation != gen) return;
                    if (!b.isEmpty()) emit self->matched(b);
                    emit self->progress(done, total);
                }, Qt::QueuedConnection);
                timer.restart();
            };
            for (int i = 0; i < commits.size(); ++i) {
                if (flag->load()) return;
                if (Search::matchesContent(repo, commits[i], spec)) batch.append(commits[i]);
                if (timer.elapsed() > 150) flush(i + 1);
            }
            flush(int(commits.size()));
        } catch (const GitError &) {
        }
        if (flag->load()) return;
        QMetaObject::invokeMethod(self, [self, gen] {
            if (self->m_generation == gen) emit self->finished();
        }, Qt::QueuedConnection);
    });
    m_thread->start();
}

} // namespace gitq
