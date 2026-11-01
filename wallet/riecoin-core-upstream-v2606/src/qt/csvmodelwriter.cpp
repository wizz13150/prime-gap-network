// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/csvmodelwriter.h>

#include <QAbstractItemModel>
#include <QFile>
#include <QDateTime>
#include <QPointer>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>

CSVModelWriter::CSVModelWriter(const QString& _filename, QObject* parent)
    : QObject(parent),
      filename(_filename)
{
}

void CSVModelWriter::setModel(const QAbstractItemModel *_model)
{
    this->model = _model;
}

void CSVModelWriter::addColumn(const QString &title, int column, int role)
{
    Column col;
    col.title = title;
    col.column = column;
    col.role = role;

    columns.append(col);
}

void CSVModelWriter::addExactAmountColumn(const QString& title, int column, int role,
                                        const std::optional<pgn::core::MonetaryContext>& context)
{
    columns.append(Column{title, column, role, true, context});
}

static void writeValue(QTextStream &f, const QString &value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    f << "\"" << escaped << "\"";
}

static void writeSep(QTextStream &f)
{
    f << ",";
}

static void writeNewline(QTextStream &f)
{
    f << "\n";
}

bool CSVModelWriter::write()
{
    m_error = WriteError::None;
    for (const auto& column : columns) if (column.exact_amount) return writeExact();

    // Legacy non-monetary exports retain their original data/escaping policy.
    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        m_error = WriteError::OpenFailed;
        return false;
    }
    QTextStream out(&file);

    int numRows = 0;
    if(model)
    {
        numRows = model->rowCount();
    }

    // Header row
    for(int i=0; i<columns.size(); ++i)
    {
        if(i!=0)
        {
            writeSep(out);
        }
        writeValue(out, columns[i].title);
    }
    writeNewline(out);

    // Data rows
    for(int j=0; j<numRows; ++j)
    {
        for(int i=0; i<columns.size(); ++i)
        {
            if(i!=0)
            {
                writeSep(out);
            }
            QVariant data = model->index(j, columns[i].column).data(columns[i].role);
            writeValue(out, data.toString());
        }
        writeNewline(out);
    }

    file.close();

    if (file.error() != QFile::NoError) m_error = WriteError::WriteFailed;
    return m_error == WriteError::None;
}

namespace {
// Classify, NEVER normalize/rewrite. Only native-formatted amount cells are
// exempt. Quoting alone does not neutralize spreadsheet formulas. Conservative
// subset, not a universal guarantee across spreadsheet import/re-save modes.
bool UnsafeMetadataPrefix(const QString& text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        char32_t codepoint = ch.unicode();
        if (ch.isHighSurrogate()) {
            if (i + 1 == text.size() || !text.at(i + 1).isLowSurrogate()) return true;
            codepoint = QChar::surrogateToUcs4(ch, text.at(++i));
        }
        const auto category = QChar::category(codepoint);
        if (category == QChar::Other_Control || category == QChar::Other_Format) return true;
        if (QChar::isSpace(codepoint)) continue;
        return codepoint == '=' || codepoint == '+' || codepoint == '-' || codepoint == '@' ||
            codepoint == 0xff1d || codepoint == 0xff0b || codepoint == 0xff0d || codepoint == 0xff20;
    }
    return false;
}

bool ValidText(const QString& text)
{
    // Do not silently replace ill-formed UTF16 or allow embedded NUL truncation.
    // Check UTF16 directly: a UTF8 decode roundtrip can consume a leading BOM
    // and is not an identity verifier for this untrusted text.
    return !text.contains(QChar(u'\0')) && text.isValidUtf16();
}

bool MetadataText(const QVariant& value, QString& text)
{
    if (!value.isValid()) return false;
    switch (value.metaType().id()) {
    case QMetaType::QString:
    case QMetaType::Bool: break;
    case QMetaType::QDateTime:
        if (!value.toDateTime().isValid()) return false;
        break;
    default: return false; // No lossy/custom/numeric conversion of metadata.
    }
    text = value.toString();
    return ValidText(text);
}
} // namespace

bool CSVModelWriter::writeExact()
{
    const auto fail = [&](WriteError error) { m_error = error; return false; };
    const auto export_columns = columns; // Stable policy/column snapshot.
    QPointer<const QAbstractItemModel> guarded_model{model};
    if (!guarded_model) return fail(WriteError::MissingModel);
    if (guarded_model->thread() != QThread::currentThread()) return fail(WriteError::WrongThread);
    const int rows = guarded_model->rowCount(), width = guarded_model->columnCount();
    if (rows < 0 || width < 0) return fail(WriteError::InvalidColumn);
    for (const auto& column : export_columns) {
        if (column.column < 0 || column.column >= width) return fail(WriteError::InvalidColumn);
        if (column.exact_amount) {
            if (!column.context) return fail(WriteError::MissingContext);
            if (pgn::core::ValidateMonetaryContext(*column.context) != pgn::core::ExactAmountError::None) return fail(WriteError::InvalidContext);
        }
        if (!ValidText(column.title)) return fail(WriteError::InvalidCell);
        if (UnsafeMetadataPrefix(column.title)) return fail(WriteError::UnsafeMetadata);
    }

    // No event pumping or wallet calls. Refuse reentrant model changes instead
    // of silently exporting a mixture of rows from different model revisions.
    bool changed{false};
    QObject watcher;
    const auto note_change = [&] { changed = true; };
    connect(model, &QAbstractItemModel::dataChanged, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::modelAboutToBeReset, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::rowsAboutToBeInserted, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::rowsAboutToBeMoved, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::columnsAboutToBeInserted, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, &watcher, note_change, Qt::DirectConnection);
    connect(model, &QAbstractItemModel::columnsAboutToBeMoved, &watcher, note_change, Qt::DirectConnection);
    const auto stable = [&] {
        return guarded_model && !changed && guarded_model->rowCount() == rows && guarded_model->columnCount() == width;
    };

    QSaveFile file(filename);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return fail(WriteError::OpenFailed);
    const auto abort = [&](WriteError error) { file.cancelWriting(); return fail(error); };
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    for (qsizetype i = 0; i < export_columns.size(); ++i) {
        if (i) writeSep(out);
        writeValue(out, export_columns[i].title);
    }
    writeNewline(out);
    for (int row = 0; row < rows; ++row) {
        if (!stable()) return abort(WriteError::ModelChanged);
        for (qsizetype i = 0; i < export_columns.size(); ++i) {
            const auto& column = export_columns[i];
            const QModelIndex index = guarded_model->index(row, column.column);
            if (!stable()) return abort(WriteError::ModelChanged);
            if (!index.isValid()) return abort(WriteError::InvalidCell);
            const QVariant value = index.data(column.role);
            if (!stable()) return abort(WriteError::ModelChanged);
            QString text;
            if (column.exact_amount) {
                if (!value.isValid() || value.isNull()) return abort(WriteError::InvalidCell);
                if (value.metaType().id() != QMetaType::LongLong) return abort(WriteError::InvalidAmountType);
                const auto formatted = pgn::core::formatExactAmount(value.value<qint64>(),
                    pgn::core::ExactAmountFormat::Delta, column.context->display, column.context->text_budget);
                if (formatted.error != pgn::core::ExactAmountError::None || !formatted.text) return abort(WriteError::InvalidAmount);
                text = QString::fromLatin1(formatted.text->data(), static_cast<qsizetype>(formatted.text->size()));
            } else {
                if (!MetadataText(value, text)) return abort(WriteError::InvalidCell);
                if (UnsafeMetadataPrefix(text)) return abort(WriteError::UnsafeMetadata);
            }
            if (i) writeSep(out);
            writeValue(out, text);
        }
        writeNewline(out);
        if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError) return abort(WriteError::WriteFailed);
    }
    if (!stable()) return abort(WriteError::ModelChanged);
    out.flush();
    if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError) return abort(WriteError::WriteFailed);
    if (!file.commit()) return fail(WriteError::CommitFailed);
    return true;
}

QString CSVModelWriter::errorString() const
{
    switch (m_error) {
    case WriteError::None: return {};
    case WriteError::MissingModel: return tr("Transaction model is unavailable.");
    case WriteError::WrongThread: return tr("Transaction model belongs to another thread.");
    case WriteError::MissingContext: return tr("Monetary context is unavailable; no amount was exported.");
    case WriteError::InvalidContext: return tr("Monetary context is invalid; no amount was exported.");
    case WriteError::InvalidColumn: return tr("CSV column metadata is invalid.");
    case WriteError::InvalidCell: return tr("A CSV cell is missing or invalid.");
    case WriteError::InvalidAmountType: return tr("An amount is not an exact signed 64-bit atomic value.");
    case WriteError::InvalidAmount: return tr("An amount cannot be represented in its monetary context.");
    case WriteError::UnsafeMetadata: return tr("CSV metadata has a potential spreadsheet-formula or unsafe leading character.");
    case WriteError::ModelChanged: return tr("Transaction model changed during export; nothing was published.");
    case WriteError::OpenFailed: return tr("Could not open an atomic CSV output file.");
    case WriteError::WriteFailed: return tr("Could not write the CSV output.");
    case WriteError::CommitFailed: return tr("Could not publish the CSV output atomically.");
    }
    return tr("Unknown CSV export failure.");
}
