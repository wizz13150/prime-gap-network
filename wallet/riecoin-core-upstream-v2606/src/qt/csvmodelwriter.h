// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CSVMODELWRITER_H
#define BITCOIN_QT_CSVMODELWRITER_H

#include <pgn/monetary_context.h>

#include <QList>
#include <QObject>
#include <QPointer>

#include <optional>

QT_BEGIN_NAMESPACE
class QAbstractItemModel;
QT_END_NAMESPACE

/** Export a Qt table model to a CSV file. This is useful for analyzing or post-processing the data in
    a spreadsheet.
 */
class CSVModelWriter : public QObject
{
    Q_OBJECT

public:
    explicit CSVModelWriter(const QString &filename, QObject *parent = nullptr);

    void setModel(const QAbstractItemModel *model);
    void addColumn(const QString &title, int column, int role=Qt::EditRole);

    /** Opt-in strict monetary export. The context is COPIED, never inferred.
     * Only a real signed qint64 QVariant is accepted as atoms. Adding this
     * column, even with missing context, forbids fallback to legacy write().
     * Other columns are untrusted metadata, not formula-capable numbers.
     */
    void addExactAmountColumn(const QString& title, int column, int role,
                             const std::optional<pgn::core::MonetaryContext>& context);

    enum class WriteError {
        None, MissingModel, WrongThread, MissingContext, InvalidContext,
        InvalidColumn, InvalidCell, InvalidAmountType, InvalidAmount,
        UnsafeMetadata, ModelChanged, OpenFailed, WriteFailed, CommitFailed,
    };
    WriteError lastError() const { return m_error; }
    QString errorString() const;

    /** Perform export of the model to CSV.
        @returns true on success, false otherwise
    */
    bool write();

private:
    QString filename;
    QPointer<const QAbstractItemModel> model;

    struct Column
    {
        QString title;
        int column;
        int role;
        bool exact_amount{false};
        std::optional<pgn::core::MonetaryContext> context;
    };
    QList<Column> columns;
    WriteError m_error{WriteError::None};
    bool writeExact();
};

#endif // BITCOIN_QT_CSVMODELWRITER_H
