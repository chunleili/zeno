#ifndef __ZENO_SEARCHBAR_H__
#define __ZENO_SEARCHBAR_H__

#include <zenoui/nodesys/nodesys_common.h>
#include <zenomodel/include/igraphsmodel.h>

class ZenoSearchBar : public QWidget
{
	Q_OBJECT

public:
    ZenoSearchBar(const QModelIndex& idx, QWidget *parentWidget = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

public slots:
    void activate();
	void onSearchForward();
	void onSearchBackward();

signals:
    void searchRequest(const QString&, SEARCH_RANGE, SEARLCH_ELEMENT, int);
    void searchReached(SEARCH_RECORD rec);

private slots:

    void onSearchExec(const QString& content);

private:
    SEARCH_RECORD _getRecord();

    SEARCH_RANGE m_range;
    SEARLCH_ELEMENT m_elem;
    QString m_content;
    QVector<SEARCH_RECORD> m_records;
    QModelIndexList m_results;
    QLineEdit* m_pLineEdit;
    QPersistentModelIndex m_index;
    int m_idx;
};


#endif