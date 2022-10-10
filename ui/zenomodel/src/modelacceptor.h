#ifndef __MODEL_ACCEPTOR_H__
#define __MODEL_ACCEPTOR_H__

#include <zenoio/acceptor/iacceptor.h>

#include "modeldata.h"

class SubGraphModel;
class GraphsModel;

class ModelAcceptor : public IAcceptor
{
public:
	ModelAcceptor(GraphsModel* pModel, bool bImport);

	//IAcceptor
	bool setLegacyDescs(const rapidjson::Value& graphObj, const NODE_DESCS& nodesParams) override;
	void BeginSubgraph(const QString& name) override;
	bool setCurrentSubGraph(IGraphsModel* pModel, const QModelIndex& subgIdx) override;
	void EndSubgraph() override;
	void setFilePath(const QString& fileName) override;
	void switchSubGraph(const QString& graphName) override;
	bool addNode(const QString& nodeid, const QString& name, const NODE_DESCS& descriptors) override;
	void setViewRect(const QRectF& rc) override;
	void setSocketKeys(const QString& id, const QStringList& keys) override;
	void initSockets(const QString& id, const QString& name, const NODE_DESCS& descs) override;
	void addDictKey(const QString& id, const QString& keyName, bool bInput) override;
	void setInputSocket(const QString &nodeCls,
		const QString &id,
		const QString &inSock,
		const QString &outId,
		const QString &outSock,
		const rapidjson::Value &defaultValue,
        const NODE_DESCS &legacyDescs) override;
	void setParamValue(const QString& id, const QString& nodeCls, const QString& name, const rapidjson::Value& value) override;
	void setPos(const QString& id, const QPointF& pos) override;
	void setOptions(const QString& id, const QStringList& options) override;
	void setColorRamps(const QString& id, const COLOR_RAMPS& colorRamps) override;
	void setBlackboard(const QString& id, const BLACKBOARD_INFO& blackboard) override;
	void setTimeInfo(const TIMELINE_INFO& info) override;
	TIMELINE_INFO timeInfo() const override;
	void setLegacyCurve(
        const QString& id,
        const QVector<QPointF>& pts,
        const QVector<QPair<QPointF, QPointF>>& hdls) override;
	QObject* currGraphObj() override;
	void endInputs(const QString& id, const QString& nodeCls) override;
	void endParams(const QString& id, const QString& nodeCls) override;

private:
    void generateLink(const QModelIndex& idx);

	TIMELINE_INFO m_timeInfo;
	SubGraphModel* m_currentGraph;
	GraphsModel* m_pModel;
	bool m_bImport;
};


#endif