#ifndef __ZENO_GRAPHS_EDITOR_H__
#define __ZENO_GRAPHS_EDITOR_H__

#include <QtWidgets>

class ZToolButton;
class ZenoSubnetListView;
class ZenoGraphsTabWidget;
class ZenoSubnetListPanel;
class ZenoGraphsLayerWidget;
class ZenoSubnetTreeView;
class ZenoWelcomePage;
class ZenoMainWindow;
class IGraphsModel;

class ZenoGraphsEditor : public QWidget
{
    Q_OBJECT
public:
    ZenoGraphsEditor(ZenoMainWindow* pMainWin);
    ~ZenoGraphsEditor();
    void resetModel(IGraphsModel* pModel);

public slots:
    void onCurrentModelClear();
    void onItemActivated(const QModelIndex& index);
    void onPageActivated(const QPersistentModelIndex& subgIdx, const QPersistentModelIndex& nodeIdx);

private slots:
    void onSubnetBtnClicked();
    void onViewBtnClicked();
    void onNewFile();

private:
    QWidget* m_pSideBar;
    ZToolButton* m_pSubnetBtn;
    ZToolButton* m_pViewBtn;
    ZenoSubnetListPanel* m_pSubnetList;
    ZenoGraphsTabWidget* m_pTabWidget;
    ZenoGraphsLayerWidget* m_pLayerWidget;
    ZenoWelcomePage* m_welcomePage;
    ZenoMainWindow* m_mainWin;
    bool m_bListView;
};

#endif
