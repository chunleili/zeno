#ifndef __ZENO_MODEL_H__
#define __ZENO_MODEL_H__

#include <zenomodel/include/igraphsmodel.h>
#include <zenoio/acceptor/iacceptor.h>

namespace zeno_model
{
    IGraphsModel* createModel(QObject* parent);
    IAcceptor* createIOAcceptor(IGraphsModel* pModel, bool bImport);
    QAbstractItemModel* treeModel(IGraphsModel* pModel, QObject* parent);
}

#endif