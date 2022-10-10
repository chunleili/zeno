#ifndef __IACCEPTOR_H__
#define __IACCEPTOR_H__

#include <rapidjson/document.h>
#include "common.h"
#include <zenomodel/include/modeldata.h>

class IGraphsModel;

class IAcceptor
{
public:
    virtual bool setLegacyDescs(const rapidjson::Value& graphObj, const NODE_DESCS &legacyDescs) = 0;
    virtual void BeginSubgraph(const QString& name) = 0;
    virtual void EndSubgraph() = 0;
    virtual bool setCurrentSubGraph(IGraphsModel* pModel, const QModelIndex& subgIdx) = 0;
    virtual void setFilePath(const QString& fileName) = 0;
    virtual void switchSubGraph(const QString& graphName) = 0;
    virtual bool addNode(const QString& nodeid, const QString& name, const NODE_DESCS& descriptors) = 0;
    virtual void setViewRect(const QRectF& rc) = 0;
    virtual void setSocketKeys(const QString& id, const QStringList& keys) = 0;
    virtual void initSockets(const QString& id, const QString& name, const NODE_DESCS& legacyDescs) = 0;
    virtual void addDictKey(const QString& id, const QString& keyName, bool bInput) = 0;
    virtual void setInputSocket(
        const QString& nodeCls,
        const QString& id,
        const QString& inSock,
        const QString& outId,
        const QString& outSock,
        const rapidjson::Value& defaultValue,
        const NODE_DESCS& legacyDescs) = 0;
    virtual void endInputs(const QString& id, const QString& nodeCls) = 0;
    virtual void setParamValue(const QString& id, const QString& nodeCls, const QString& name, const rapidjson::Value& value) = 0;
    virtual void endParams(const QString& id, const QString& nodeCls) = 0;
    virtual void setPos(const QString& id, const QPointF& pos) = 0;
    virtual void setOptions(const QString& id, const QStringList& options) = 0;
    virtual void setColorRamps(const QString& id, const COLOR_RAMPS& colorRamps) = 0;
    virtual void setBlackboard(const QString& id, const BLACKBOARD_INFO& blackboard) = 0;
    virtual void setTimeInfo(const TIMELINE_INFO& info) = 0;
    virtual TIMELINE_INFO timeInfo() const = 0;
    virtual void setLegacyCurve(
        const QString& id,
        const QVector<QPointF>& pts,
        const QVector<QPair<QPointF, QPointF>>& hdls) = 0;
    virtual QObject* currGraphObj() = 0;
    virtual ~IAcceptor() = default;
};


#endif
