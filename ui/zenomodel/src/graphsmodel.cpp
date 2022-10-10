#include "subgraphmodel.h"
#include "graphsmodel.h"
#include "modelrole.h"
#include "uihelper.h"
#include "nodesmgr.h"
#include "zassert.h"
#include <zeno/zeno.h>
#include <zenoui/util/cihou.h>
#include <zeno/utils/scope_exit.h>


class ApiLevelScope
{
public:
    ApiLevelScope(GraphsModel* pModel) : m_model(pModel)
    {
        m_model->beginApiLevel();
    }
    ~ApiLevelScope()
    {
        m_model->endApiLevel();
    }
private:
    GraphsModel* m_model;
};


GraphsModel::GraphsModel(QObject *parent)
    : IGraphsModel(parent)
    , m_selection(nullptr)
    , m_dirty(false)
    , m_linkModel(new QStandardItemModel(this))
    , m_stack(new QUndoStack(this))
    , m_apiLevel(0)
    , m_bIOProcessing(false)
{
    m_selection = new QItemSelectionModel(this);

    //link sync:
    connect(m_linkModel, &QAbstractItemModel::dataChanged, this, &GraphsModel::on_linkDataChanged);
    connect(m_linkModel, &QAbstractItemModel::rowsAboutToBeInserted, this, &GraphsModel::on_linkAboutToBeInserted);
    connect(m_linkModel, &QAbstractItemModel::rowsInserted, this, &GraphsModel::on_linkInserted);
    connect(m_linkModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &GraphsModel::on_linkAboutToBeRemoved);
    connect(m_linkModel, &QAbstractItemModel::rowsRemoved, this, &GraphsModel::on_linkRemoved);

    initDescriptors();
}

GraphsModel::~GraphsModel()
{
    clear();
}

QItemSelectionModel* GraphsModel::selectionModel() const
{
    return m_selection;
}

void GraphsModel::setFilePath(const QString& fn)
{
    m_filePath = fn;
    emit pathChanged(m_filePath);
}

SubGraphModel* GraphsModel::subGraph(const QString& name) const
{
    for (int i = 0; i < m_subGraphs.size(); i++)
    {
        if (m_subGraphs[i]->name() == name)
            return m_subGraphs[i];
    }
    return nullptr;
}

SubGraphModel* GraphsModel::subGraph(int idx) const
{
    if (idx >= 0 && idx < m_subGraphs.count())
    {
        return m_subGraphs[idx];
    }
    return nullptr;
}

SubGraphModel* GraphsModel::currentGraph()
{
    return subGraph(m_selection->currentIndex().row());
}

void GraphsModel::switchSubGraph(const QString& graphName)
{
    QModelIndex startIndex = createIndex(0, 0, nullptr);
    const QModelIndexList &lst = this->match(startIndex, ROLE_OBJNAME, graphName, 1, Qt::MatchExactly);
    if (lst.size() == 1)
    {
        m_selection->setCurrentIndex(lst[0], QItemSelectionModel::Current);
    }
}

void GraphsModel::initMainGraph()
{
    SubGraphModel* subGraphModel = new SubGraphModel(this);
    subGraphModel->setName("main");
    appendSubGraph(subGraphModel);
}

void GraphsModel::newSubgraph(const QString &graphName)
{
    if (graphName.compare("main", Qt::CaseInsensitive) == 0)
    {
        zeno::log_error("main graph is not allowed to be created or removed");
        return;
    }

    if (m_nodesDesc.find(graphName) != m_nodesDesc.end() ||
        m_subgsDesc.find(graphName) != m_subgsDesc.end())
    {
        zeno::log_error("Already has a graph or node called \"{}\"", graphName.toStdString());
        return;
    }

    QModelIndex startIndex = createIndex(0, 0, nullptr);
    const QModelIndexList &lst = this->match(startIndex, ROLE_OBJNAME, graphName, 1, Qt::MatchExactly);
    if (lst.size() == 1)
    {
        m_selection->setCurrentIndex(lst[0], QItemSelectionModel::Current);
    }
    else
    {
        SubGraphModel *subGraphModel = new SubGraphModel(this);
        subGraphModel->setName(graphName);
        appendSubGraph(subGraphModel);
        m_selection->setCurrentIndex(index(rowCount() - 1, 0), QItemSelectionModel::Current);
        markDirty();
    }
}

void GraphsModel::renameSubGraph(const QString& oldName, const QString& newName)
{
    if (oldName == newName || oldName.compare("main", Qt::CaseInsensitive) == 0)
        return;

    ZASSERT_EXIT(m_subgsDesc.find(oldName) != m_subgsDesc.end() &&
        m_subgsDesc.find(newName) == m_subgsDesc.end());

    //todo: transaction.
    SubGraphModel* pSubModel = subGraph(oldName);
    ZASSERT_EXIT(pSubModel);
    pSubModel->setName(newName);

    for (int r = 0; r < this->rowCount(); r++)
    {
        SubGraphModel* pModel = subGraph(r);
        ZASSERT_EXIT(pModel);
        const QString& subgraphName = pModel->name();
        if (subgraphName != oldName)
        {
            pModel->replaceSubGraphNode(oldName, newName);
        }
    }

    NODE_DESC desc = m_subgsDesc[oldName];
    m_subgsDesc[newName] = desc;
    m_subgsDesc.remove(oldName);

    uint32_t ident = m_name2id[oldName];
    m_id2name[ident] = newName;
    ZASSERT_EXIT(m_name2id.find(oldName) != m_name2id.end());
    m_name2id.remove(oldName);
    m_name2id[newName] = ident;

    for (QString cate : desc.categories)
    {
        m_nodesCate[cate].nodes.removeAll(oldName);
        m_nodesCate[cate].nodes.append(newName);
    }

    emit graphRenamed(oldName, newName);
}

QModelIndex GraphsModel::nodeIndex(uint32_t id)
{
    for (int row = 0; row < m_subGraphs.size(); row++)
    {
        QModelIndex idx = m_subGraphs[row]->index(id);
        if (idx.isValid())
            return idx;
    }
    return QModelIndex();
}

QModelIndex GraphsModel::subgIndex(uint32_t sid)
{
    ZASSERT_EXIT(m_id2name.find(sid) != m_id2name.end(), QModelIndex());
    const QString& subgName = m_id2name[sid];
    return index(subgName);
}

QModelIndex GraphsModel::subgByNodeId(uint32_t id)
{
    for (int row = 0; row < m_subGraphs.size(); row++)
    {
        if (m_subGraphs[row]->index(id).isValid())
            return index(row, 0);
    }
    return QModelIndex();
}

QModelIndex GraphsModel::_createIndex(SubGraphModel* pSubModel) const
{
    if (!pSubModel)
        return QModelIndex();

    const QString& subgName = pSubModel->name();
    ZASSERT_EXIT(m_name2id.find(subgName) != m_name2id.end(), QModelIndex());
    int row = m_subGraphs.indexOf(pSubModel);
    return createIndex(row, 0, m_name2id[subgName]);
}

QModelIndex GraphsModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || row >= m_subGraphs.size())
        return QModelIndex();

    return _createIndex(m_subGraphs[row]);
}

QModelIndex GraphsModel::index(const QString& subGraphName) const
{
	for (int row = 0; row < m_subGraphs.size(); row++)
	{
		if (m_subGraphs[row]->name() == subGraphName)
		{
            return _createIndex(m_subGraphs[row]);
		}
	}
    return QModelIndex();
}

QModelIndex GraphsModel::indexBySubModel(SubGraphModel* pSubModel) const
{
    for (int row = 0; row < m_subGraphs.size(); row++)
    {
        if (m_subGraphs[row] == pSubModel)
            return _createIndex(pSubModel);
    }
    return QModelIndex();
}

QModelIndex GraphsModel::linkIndex(int r)
{
    return m_linkModel->index(r, 0);
}

QModelIndex GraphsModel::linkIndex(const QString& outNode,
                                   const QString& outSock,
                                   const QString& inNode,
                                   const QString& inSock)
{
    if (m_linkModel == nullptr)
        return QModelIndex();
    for (int r = 0; r < m_linkModel->rowCount(); r++)
    {
        QModelIndex idx = m_linkModel->index(r, 0);
        if (outNode == idx.data(ROLE_OUTNODE).toString() &&
            outSock == idx.data(ROLE_OUTSOCK).toString() &&
            inNode == idx.data(ROLE_INNODE).toString() &&
            inSock == idx.data(ROLE_INSOCK).toString())
        {
            return idx;
        }
    }
    return QModelIndex();
}

QModelIndex GraphsModel::parent(const QModelIndex& child) const
{
    return QModelIndex();
}

QVariant GraphsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    switch (role)
    {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case ROLE_OBJNAME:
        return m_subGraphs[index.row()]->name();
    }
    return QVariant();
}

int GraphsModel::rowCount(const QModelIndex& parent) const
{
    return m_subGraphs.size();
}

int GraphsModel::columnCount(const QModelIndex& parent) const
{
    return 1;
}

Qt::ItemFlags GraphsModel::flags(const QModelIndex& index) const
{
    return IGraphsModel::flags(index) | Qt::ItemIsEditable;
}

bool GraphsModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role == Qt::EditRole)
	{
		const QString& newName = value.toString();
		const QString& oldName = data(index, Qt::DisplayRole).toString();
		if (newName != oldName)
		{
			SubGraphModel* pModel = subGraph(oldName);
			if (!oldName.isEmpty())
			{
				renameSubGraph(oldName, newName);
			}
		}
	}
	return false;
}

void GraphsModel::revert(const QModelIndex& idx)
{
	const QString& subgName = idx.data().toString();
	if (subgName.isEmpty())
	{
		//exitting new item
        removeGraph(idx.row());
	}
}

NODE_DESCS GraphsModel::getCoreDescs()
{
	NODE_DESCS descs;
	QString strDescs = QString::fromStdString(zeno::getSession().dumpDescriptors());
    //zeno::log_critical("EEEE {}", strDescs.toStdString());
    //ZENO_P(strDescs.toStdString());
	QStringList L = strDescs.split("\n");
	for (int i = 0; i < L.size(); i++)
	{
		QString line = L[i];
		if (line.startsWith("DESC@"))
		{
			line = line.trimmed();
			int idx1 = line.indexOf("@");
			int idx2 = line.indexOf("@", idx1 + 1);
			ZASSERT_EXIT(idx1 != -1 && idx2 != -1, descs);
			QString wtf = line.mid(0, idx1);
			QString z_name = line.mid(idx1 + 1, idx2 - idx1 - 1);
			QString rest = line.mid(idx2 + 1);
			ZASSERT_EXIT(rest.startsWith("{") && rest.endsWith("}"), descs);
			auto _L = rest.mid(1, rest.length() - 2).split("}{");
			QString inputs = _L[0], outputs = _L[1], params = _L[2], categories = _L[3];
			QStringList z_categories = categories.split('%', QtSkipEmptyParts);

			NODE_DESC desc;
			for (QString input : inputs.split("%", QtSkipEmptyParts))
			{
				QString type, name, defl;
				auto _arr = input.split('@');
				ZASSERT_EXIT(_arr.size() == 3, descs);
				type = _arr[0];
				name = _arr[1];
				defl = _arr[2];
				INPUT_SOCKET socket;
				socket.info.type = type;
				socket.info.name = name;
                socket.info.control = UiHelper::getControlType(type);
				socket.info.defaultValue = UiHelper::parseStringByType(defl, type);
				desc.inputs[name] = socket;
			}
			for (QString output : outputs.split("%", QtSkipEmptyParts))
			{
				QString type, name, defl;
				auto _arr = output.split('@');
				ZASSERT_EXIT(_arr.size() == 3, descs);
				type = _arr[0];
				name = _arr[1];
				defl = _arr[2];
				OUTPUT_SOCKET socket;
				socket.info.type = type;
				socket.info.name = name;
                socket.info.control = UiHelper::getControlType(type);
				socket.info.defaultValue = UiHelper::parseStringByType(defl, type);
				desc.outputs[name] = socket;
			}
			for (QString param : params.split("%", QtSkipEmptyParts))
			{
				QString type, name, defl;
				auto _arr = param.split('@');
				type = _arr[0];
				name = _arr[1];
				defl = _arr[2];
				PARAM_INFO paramInfo;
				paramInfo.bEnableConnect = false;
				paramInfo.name = name;
				paramInfo.typeDesc = type;
				paramInfo.control = UiHelper::getControlType(type);
				paramInfo.defaultValue = UiHelper::parseStringByType(defl, type);
				//thers is no "value" in descriptor, but it's convient to initialize param value. 
				paramInfo.value = paramInfo.defaultValue;
				desc.params[name] = paramInfo;
			}
			desc.categories = z_categories;
            desc.name = z_name;

			descs.insert(z_name, desc);
		}
	}
    return descs;
}

void GraphsModel::initDescriptors()
{
    m_nodesDesc = getCoreDescs();    m_nodesCate.clear();
    for (auto it = m_nodesDesc.constBegin(); it != m_nodesDesc.constEnd(); it++)
    {
        const QString& name = it.key();
        const NODE_DESC& desc = it.value();
        registerCate(desc);
    }

    //add Blackboard
    NODE_DESC desc;
    desc.name = "Blackboard";
    desc.categories.push_back("layout");
    m_nodesDesc.insert(desc.name, desc);
    registerCate(desc);
}

NODE_DESC GraphsModel::getSubgraphDesc(SubGraphModel* pModel)
{
    const QString& graphName = pModel->name();
    if (graphName == "main" || graphName.isEmpty())
        return NODE_DESC();

    QString subcategory = "subgraph";
    INPUT_SOCKETS subInputs;
    OUTPUT_SOCKETS subOutputs;
    for (int i = 0; i < pModel->rowCount(); i++)
    {
        QModelIndex idx = pModel->index(i, 0);
        const QString& nodeName = idx.data(ROLE_OBJNAME).toString();
        PARAMS_INFO params = idx.data(ROLE_PARAMETERS).value<PARAMS_INFO>();
        if (nodeName == "SubInput")
        {
            QString n_type = params["type"].value.toString();
            QString n_name = params["name"].value.toString();
            auto n_defl = params["defl"].value;

            SOCKET_INFO info;
            info.name = n_name;
            info.type = n_type;
            info.defaultValue = n_defl; 

            INPUT_SOCKET inputSock;
            inputSock.info = info;
            subInputs.insert(n_name, inputSock);
        }
        else if (nodeName == "SubOutput")
        {
            auto n_type = params["type"].value.toString();
            auto n_name = params["name"].value.toString();
            auto n_defl = params["defl"].value;

            SOCKET_INFO info;
            info.name = n_name;
            info.type = n_type;
            info.defaultValue = n_defl; 

            OUTPUT_SOCKET outputSock;
            outputSock.info = info;
            subOutputs.insert(n_name, outputSock);
        }
        else if (nodeName == "SubCategory")
        {
            subcategory = params["name"].value.toString();
        }
    }

    INPUT_SOCKET srcSock;
    srcSock.info.name = "SRC";
    OUTPUT_SOCKET dstSock;
    dstSock.info.name = "DST";

    subInputs.insert("SRC", srcSock);
    subOutputs.insert("DST", dstSock);

    NODE_DESC desc;
    desc.inputs = subInputs;
    desc.outputs = subOutputs;
    desc.categories.push_back(subcategory);
    desc.is_subgraph = true;
    desc.name = graphName;

    return desc;
}

NODE_DESCS GraphsModel::descriptors() const
{
    NODE_DESCS descs;
    for (QString descName : m_subgsDesc.keys())
    {
        descs.insert(descName, m_subgsDesc[descName]);
    }
    for (QString nodeName : m_nodesDesc.keys())
    {
        //subgraph node has high priority than core node.
        if (descs.find(nodeName) == descs.end())
        {
            descs.insert(nodeName, m_nodesDesc[nodeName]);
        }
    }
    return descs;
}

bool GraphsModel::appendSubnetDescsFromZsg(const QList<NODE_DESC>& zsgSubnets)
{
    for (NODE_DESC desc : zsgSubnets)
    {
        if (m_subgsDesc.find(desc.name) == m_subgsDesc.end())
        {
            desc.is_subgraph = true;
            m_subgsDesc.insert(desc.name, desc);
            registerCate(desc);
        }
        else
        {
            zeno::log_error("The graph \"{}\" exists!", desc.name.toStdString());
            return false;
        }
    }
    return true;
}

void GraphsModel::registerCate(const NODE_DESC& desc)
{
    for (auto cate : desc.categories)
    {
        m_nodesCate[cate].name = cate;
        m_nodesCate[cate].nodes.push_back(desc.name);
    }
}

bool GraphsModel::getDescriptor(const QString& descName, NODE_DESC& desc)
{
    //internal node or subgraph node? if same name.
    if (m_subgsDesc.find(descName) != m_subgsDesc.end())
    {
        desc = m_subgsDesc[descName];
        return true;
    }
    if (m_nodesDesc.find(descName) != m_nodesDesc.end())
    {
        desc = m_nodesDesc[descName];
        return true;
    }
    return false;
}

void GraphsModel::appendSubGraph(SubGraphModel* pGraph)
{
    int row = m_subGraphs.size();
	beginInsertRows(QModelIndex(), row, row);
    m_subGraphs.append(pGraph);

    const QString& name = pGraph->name();
    QUuid uuid = QUuid::createUuid();
    uint32_t ident = uuid.data1;
    m_id2name[ident] = name;
    m_name2id[name] = ident;

	endInsertRows();
    //the subgraph desc has been inited when processing io.
    if (!IsIOProcessing())
    {
        NODE_DESC desc = getSubgraphDesc(pGraph);
        if (!desc.name.isEmpty() && m_subgsDesc.find(desc.name) == m_subgsDesc.end())
        {
            m_subgsDesc.insert(desc.name, desc);
            registerCate(desc);
        }
    }
}

void GraphsModel::removeGraph(int idx)
{
    beginRemoveRows(QModelIndex(), idx, idx);

    const QString& descName = m_subGraphs[idx]->name();
    m_subGraphs.remove(idx);

    ZASSERT_EXIT(m_name2id.find(descName) != m_name2id.end());
    uint32_t ident = m_name2id[descName];
    m_name2id.remove(descName);
    ZASSERT_EXIT(m_id2name.find(ident) != m_id2name.end());
    m_id2name.remove(ident);

    endRemoveRows();

    //if there is a core node shared the same name with this subgraph,
    // it will not be exported because it was omitted at begin.
    ZASSERT_EXIT(m_subgsDesc.find(descName) != m_subgsDesc.end());
    NODE_DESC desc = m_subgsDesc[descName];
    m_subgsDesc.remove(descName);
    for (QString cate : desc.categories)
    {
        m_nodesCate[cate].nodes.removeAll(descName);
    }
    markDirty();
}

QModelIndex GraphsModel::fork(const QModelIndex& subgIdx, const QModelIndex &subnetNodeIdx)
{
    const QString& subnetName = subnetNodeIdx.data(ROLE_OBJNAME).toString();
    SubGraphModel* pModel = subGraph(subnetName);
    ZASSERT_EXIT(pModel, QModelIndex());

    NODE_DATA subnetData = _fork(subnetName);
    SubGraphModel *pCurrentModel = subGraph(subgIdx.row());
    pCurrentModel->appendItem(subnetData, false);

    QModelIndex newForkNodeIdx = pCurrentModel->index(subnetData[ROLE_OBJID].toString());
    return newForkNodeIdx;
}

NODE_DATA GraphsModel::_fork(const QString& forkSubgName)
{
    SubGraphModel* pModel = subGraph(forkSubgName);
    ZASSERT_EXIT(pModel, NODE_DATA());

    QMap<QString, NODE_DATA> nodes;
    QMap<QString, NODE_DATA> oldGraphsToNew;
    QList<EdgeInfo> links;
    for (int r = 0; r < pModel->rowCount(); r++)
    {
        QModelIndex newIdx;
        QModelIndex idx = pModel->index(r, 0);
        NODE_DATA data;
        if (IsSubGraphNode(idx))
        {
            const QString& snodeId = idx.data(ROLE_OBJID).toString();
            const QString& ssubnetName = idx.data(ROLE_OBJNAME).toString();
            SubGraphModel* psSubModel = subGraph(ssubnetName);
            ZASSERT_EXIT(psSubModel, NODE_DATA());
            data = _fork(ssubnetName);
            const QString &subgNewNodeId = data[ROLE_OBJID].toString();

            nodes.insert(snodeId, pModel->itemData(idx));
            oldGraphsToNew.insert(snodeId, data);
        }
        else
        {
            data = pModel->itemData(idx);
            const QString &ident = idx.data(ROLE_OBJID).toString();
            nodes.insert(ident, data);
        }
    }
    for (int r = 0; r < m_linkModel->rowCount(); r++)
    {
        QModelIndex idx = m_linkModel->index(r, 0);
        const QString& outNode = idx.data(ROLE_OUTNODE).toString();
        const QString& inNode = idx.data(ROLE_INNODE).toString();
        if (nodes.find(inNode) != nodes.end() && nodes.find(outNode) != nodes.end())
        {
            const QString& outSock = idx.data(ROLE_OUTSOCK).toString();
            const QString& inSock = idx.data(ROLE_INSOCK).toString();
            links.append(EdgeInfo(outNode, inNode, outSock, inSock));
        }
    }

    const QString& forkName = uniqueSubgraph(forkSubgName);
    SubGraphModel* pForkModel = new SubGraphModel(this);
    pForkModel->setName(forkName);
    appendSubGraph(pForkModel);
    UiHelper::reAllocIdents(nodes, links, oldGraphsToNew);

    QModelIndex newSubgIdx = indexBySubModel(pForkModel);

    // import nodes and links into the new created subgraph.
    importNodes(nodes, links, QPointF(), newSubgIdx, false);

    //create the new fork subnet node at outter layer.
    NODE_DATA subnetData = NodesMgr::newNodeData(this, forkSubgName);
    subnetData[ROLE_OBJID] = UiHelper::generateUuid(forkName);
    subnetData[ROLE_OBJNAME] = forkName;
    //clear the link.
    OUTPUT_SOCKETS outputs = subnetData[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();
    for (auto it = outputs.begin(); it != outputs.end(); it++) {
        it->second.linkIndice.clear();
        it->second.inNodes.clear();
    }
    INPUT_SOCKETS inputs = subnetData[ROLE_INPUTS].value<INPUT_SOCKETS>();
    for (auto it = inputs.begin(); it != inputs.end(); it++) {
        it->second.linkIndice.clear();
        it->second.outNodes.clear();
    }
    subnetData[ROLE_INPUTS] = QVariant::fromValue(inputs);
    subnetData[ROLE_OUTPUTS] = QVariant::fromValue(outputs);
    //temp code: node pos.
    QPointF pos = subnetData[ROLE_OBJPOS].toPointF();
    pos.setY(pos.y() + 100);
    subnetData[ROLE_OBJPOS] = pos;
    return subnetData;
}

QString GraphsModel::uniqueSubgraph(QString orginName)
{
    QString newSubName = orginName;
    while (subGraph(newSubName)) {
        newSubName = UiHelper::nthSerialNumName(newSubName);
    }
    return newSubName;
}

NODE_CATES GraphsModel::getCates()
{
    return m_nodesCate;
}

QString GraphsModel::filePath() const
{
    return m_filePath;
}

QString GraphsModel::fileName() const
{
    QFileInfo fi(m_filePath);
    QString fn;
    if (fi.isFile())
        fn = fi.fileName();
    return fn;
}

void GraphsModel::onCurrentIndexChanged(int row)
{
    const QString& graphName = data(index(row, 0), ROLE_OBJNAME).toString();
    switchSubGraph(graphName);
}

bool GraphsModel::isDirty() const
{
    return m_dirty;
}

void GraphsModel::markDirty()
{
    m_dirty = true;
    emit dirtyChanged();
}

void GraphsModel::clearDirty()
{
    m_dirty = false;
    emit dirtyChanged();
}

void GraphsModel::onRemoveCurrentItem()
{
    removeGraph(m_selection->currentIndex().row());
    //switch to main.
    QModelIndex startIndex = createIndex(0, 0, nullptr);
    //to ask: if not main scene?
    const QModelIndexList &lst = this->match(startIndex, ROLE_OBJNAME, "main", 1, Qt::MatchExactly);
    if (lst.size() == 1)
        m_selection->setCurrentIndex(index(lst[0].row(), 0), QItemSelectionModel::Current);
}

void GraphsModel::beginTransaction(const QString& name)
{
    m_stack->beginMacro(name);
    beginApiLevel();
}

void GraphsModel::endTransaction()
{
    m_stack->endMacro();
    endApiLevel();
}

void GraphsModel::beginApiLevel()
{
    if (IsIOProcessing())
        return;

    //todo: Thread safety
    m_apiLevel++;
}

void GraphsModel::endApiLevel()
{
    if (IsIOProcessing())
        return;

    m_apiLevel--;
    if (m_apiLevel == 0)
    {
        onApiBatchFinished();
    }
}

void GraphsModel::undo()
{
    ApiLevelScope batch(this);
    m_stack->undo();
}

void GraphsModel::redo()
{
    ApiLevelScope batch(this);
    m_stack->redo();
}

void GraphsModel::onApiBatchFinished()
{
    emit apiBatchFinished();
}

QModelIndex GraphsModel::index(const QString& id, const QModelIndex& subGpIdx)
{
    SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph, QModelIndex());
    // index of SubGraph rather than Graphs.
    return pGraph->index(id);
}

QModelIndex GraphsModel::index(int r, const QModelIndex& subGpIdx)
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph, QModelIndex());
	// index of SubGraph rather than Graphs.
	return pGraph->index(r, 0);
}

int GraphsModel::itemCount(const QModelIndex& subGpIdx) const
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph, 0);
    return pGraph->rowCount();
}

void GraphsModel::addNode(const NODE_DATA& nodeData, const QModelIndex& subGpIdx, bool enableTransaction)
{
    //TODO: add this at the beginning of all apis?
    bool bEnableIOProc = IsIOProcessing();
    if (bEnableIOProc)
        enableTransaction = false;

    if (enableTransaction)
    {
        QString id = nodeData[ROLE_OBJID].toString();
        AddNodeCommand* pCmd = new AddNodeCommand(id, nodeData, this, subGpIdx);
        m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

        SubGraphModel* pGraph = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pGraph);

        NODE_DATA nodeData2 = nodeData;
        PARAMS_INFO params = nodeData2[ROLE_PARAMETERS].value<PARAMS_INFO>();
        QString descName = nodeData[ROLE_OBJNAME].toString();
        if (descName == "SubInput" || descName == "SubOutput")
        {
            ZASSERT_EXIT(params.find("name") != params.end());
            PARAM_INFO& param = params["name"];
            QString newSockName =
                UiHelper::correctSubIOName(this, pGraph->name(), param.value.toString(), descName == "SubInput");
            param.value = newSockName;
            nodeData2[ROLE_PARAMETERS] = QVariant::fromValue(params);
            pGraph->appendItem(nodeData2);
        }
        else
        {
            if (descName == "MakeList" || descName == "MakeDict")
            {
                INPUT_SOCKETS inputs = nodeData2[ROLE_INPUTS].value<INPUT_SOCKETS>();
                INPUT_SOCKET inSocket;
                inSocket.info.nodeid = nodeData2[ROLE_OBJID].toString();

                int maxObjId = UiHelper::getMaxObjId(inputs.keys());
                if (maxObjId == -1)
                {
                    inSocket.info.name = "obj0";
                    if (descName == "MakeDict")
                    {
                        inSocket.info.control = CONTROL_DICTKEY;
                    }
                    inputs.insert(inSocket.info.name, inSocket);
                    nodeData2[ROLE_INPUTS] = QVariant::fromValue(inputs);
                }
            }
            else if (descName == "ExtractDict")
            {
                OUTPUT_SOCKETS outputs = nodeData2[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();
                OUTPUT_SOCKET outSocket;
                outSocket.info.nodeid = nodeData2[ROLE_OBJID].toString();

                int maxObjId = UiHelper::getMaxObjId(outputs.keys());
                if (maxObjId == -1)
                {
                    outSocket.info.name = "obj0";
                    outSocket.info.control = CONTROL_DICTKEY;
                    outputs.insert(outSocket.info.name, outSocket);
                    nodeData2[ROLE_OUTPUTS] = QVariant::fromValue(outputs);
                }
            }

            pGraph->appendItem(nodeData2);
        }

        //todo: update desc if meet subinput/suboutput node, but pay attention to main graph.
        if (!bEnableIOProc)
        {
            const QModelIndex &idx = pGraph->index(nodeData[ROLE_OBJID].toString());
            const QString &objName = idx.data(ROLE_OBJNAME).toString();
            bool bInserted = true;
            if (objName == "SubInput")
                onSubIOAddRemove(pGraph, idx, true, bInserted);
            else if (objName == "SubOutput") 
                onSubIOAddRemove(pGraph, idx, false, bInserted);
        }
    }
}

void GraphsModel::removeNode(const QString& nodeid, const QModelIndex& subGpIdx, bool enableTransaction)
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph);

    bool bEnableIOProc = IsIOProcessing();
    if (bEnableIOProc)
        enableTransaction = false;

    if (enableTransaction)
    {
        QModelIndex idx = pGraph->index(nodeid);
        int row = idx.row();
        const NODE_DATA& data = pGraph->itemData(idx);

        RemoveNodeCommand* pCmd = new RemoveNodeCommand(row, data, this, subGpIdx);
        m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

        const QModelIndex &idx = pGraph->index(nodeid);
        const QString &objName = idx.data(ROLE_OBJNAME).toString();
        if (!bEnableIOProc)
        {
            //if subnode removed, the whole graphs referred to it should be update.
            bool bInserted = false;
            if (objName == "SubInput")
            {
                onSubIOAddRemove(pGraph, idx, true, bInserted);
            }
            else if (objName == "SubOutput")
            {
                onSubIOAddRemove(pGraph, idx, false, bInserted);
            }
        }
        pGraph->removeNode(nodeid, false);
    }
}

void GraphsModel::appendNodes(const QList<NODE_DATA>& nodes, const QModelIndex& subGpIdx, bool enableTransaction)
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph);
    for (const NODE_DATA& nodeData : nodes)
    {
        addNode(nodeData, subGpIdx, enableTransaction);
    }
}

void GraphsModel::importNodeLinks(const QList<NODE_DATA>& nodes, const QModelIndex& subGpIdx)
{
	beginTransaction("import nodes");

	appendNodes(nodes, subGpIdx, true);
	//add links for pasted node.
	for (int i = 0; i < nodes.size(); i++)
	{
		const NODE_DATA& data = nodes[i];
		const QString& inNode = data[ROLE_OBJID].toString();
		INPUT_SOCKETS inputs = data[ROLE_INPUTS].value<INPUT_SOCKETS>();
		foreach(const QString & inSockName, inputs.keys())
		{
			const INPUT_SOCKET& inSocket = inputs[inSockName];
			for (const QString& outNode : inSocket.outNodes.keys())
			{
				for (const QString& outSock : inSocket.outNodes[outNode].keys())
				{
					const QModelIndex& outIdx = index(outNode, subGpIdx);
					if (outIdx.isValid())
					{
						addLink(EdgeInfo(outNode, inNode, outSock, inSockName), subGpIdx, false, true);
					}
				}
			}
		}
	}
    endTransaction();
}

void GraphsModel::importNodes(
                const QMap<QString, NODE_DATA>& nodes,
                const QList<EdgeInfo>& links,
                const QPointF& pos,
                const QModelIndex& subGpIdx,
                bool enableTransaction)
{
    if (nodes.isEmpty()) return;
    //ZASSERT_EXIT(!nodes.isEmpty());
    if (enableTransaction)
    {
        ImportNodesCommand *pCmd = new ImportNodesCommand(nodes, links, pos, this, subGpIdx);
        m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

        SubGraphModel* pGraph = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pGraph);
        for (const NODE_DATA& data : nodes)
        {
            addNode(data, subGpIdx, false);
        }

        //resolve pos and links.
        QStringList ids = nodes.keys();
        QPointF _pos = pGraph->getNodeStatus(ids[0], ROLE_OBJPOS).toPointF();
        const QPointF offset = pos - _pos;

        for (const QString& ident : ids)
	    {
		    const QModelIndex& idx = pGraph->index(ident);
		    _pos = idx.data(ROLE_OBJPOS).toPointF();
		    _pos += offset;
		    pGraph->setData(idx, _pos, ROLE_OBJPOS);
	    }
        for (EdgeInfo link : links)
        {
            addLink(link, subGpIdx, false, false);
        }
    }
}

void GraphsModel::copyPaste(const QModelIndex &fromSubg, const QModelIndexList &srcNodes, const QModelIndex &toSubg, QPointF pos, bool enableTrans)
{
    if (!fromSubg.isValid() || srcNodes.isEmpty() || !toSubg.isValid())
        return;

    if (enableTrans)
        beginTransaction("copy paste");

    SubGraphModel* srcGraph = subGraph(fromSubg.row());
    ZASSERT_EXIT(srcGraph);

    SubGraphModel* dstGraph = subGraph(toSubg.row());
    ZASSERT_EXIT(dstGraph);

    QMap<QString, QString> old2New, new2old;

    QMap<QString, NODE_DATA> oldNodes;
    for (QModelIndex idx : srcNodes)
    {
        NODE_DATA old = srcGraph->itemData(idx);
        oldNodes.insert(old[ROLE_OBJID].toString(), old);
    }
    QPointF offset = pos - (*oldNodes.begin())[ROLE_OBJPOS].toPointF();

    QMap<QString, NODE_DATA> newNodes;
    for (NODE_DATA old : oldNodes)
    {
        NODE_DATA newNode = old;
        const INPUT_SOCKETS inputs = newNode[ROLE_INPUTS].value<INPUT_SOCKETS>();
        INPUT_SOCKETS newInputs = inputs;
        const OUTPUT_SOCKETS outputs = newNode[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();
        OUTPUT_SOCKETS newOutputs = outputs;

        for (INPUT_SOCKET& inSocket : newInputs)
        {
            inSocket.linkIndice.clear();
            inSocket.outNodes.clear();
        }
        newNode[ROLE_INPUTS] = QVariant::fromValue(newInputs);

        for (OUTPUT_SOCKET& outSocket : newOutputs)
        {
            outSocket.linkIndice.clear();
            outSocket.inNodes.clear();
        }
        newNode[ROLE_OUTPUTS] = QVariant::fromValue(newOutputs);

        QString nodeName = old[ROLE_OBJNAME].toString();
        const QString& oldId = old[ROLE_OBJID].toString();
        const QString& newId = UiHelper::generateUuid(nodeName);

        newNode[ROLE_OBJPOS] = old[ROLE_OBJPOS].toPointF() + offset;
        newNode[ROLE_OBJID] = newId;

        newNodes.insert(newId, newNode);

        old2New.insert(oldId, newId);
        new2old.insert(newId, oldId);
    }

    QList<NODE_DATA> lstNodes;
    for (NODE_DATA data : newNodes)
        lstNodes.append(data);
    appendNodes(lstNodes, toSubg, enableTrans);

    //reconstruct topology for new node.
    for (NODE_DATA newNode : newNodes)
    {
        const QString& newId = newNode[ROLE_OBJID].toString();
        const QString& oldId = new2old[newId];

        const NODE_DATA& oldData = oldNodes[oldId];

        const INPUT_SOCKETS &oldInputs = oldData[ROLE_INPUTS].value<INPUT_SOCKETS>();
        const OUTPUT_SOCKETS &oldOutputs = oldData[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();

        INPUT_SOCKETS inputs = newNode[ROLE_INPUTS].value<INPUT_SOCKETS>();
        OUTPUT_SOCKETS outputs = newNode[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();

        for (INPUT_SOCKET inSock : oldInputs)
        {
            for (QPersistentModelIndex linkIdx : inSock.linkIndice)
            {
                QString inNode = linkIdx.data(ROLE_INNODE).toString();
                QString inSock = linkIdx.data(ROLE_INSOCK).toString();
                QString outNode = linkIdx.data(ROLE_OUTNODE).toString();
                QString outSock = linkIdx.data(ROLE_OUTSOCK).toString();

                if (oldNodes.find(inNode) != oldNodes.end() && oldNodes.find(outNode) != oldNodes.end())
                {
                    QString newOutNode, newInNode;
                    newOutNode = old2New[outNode];
                    newInNode = old2New[inNode];
                    addLink(EdgeInfo(newOutNode, newInNode, outSock, inSock), toSubg, false, enableTrans);
                }
            }
        }
    }
    if (enableTrans)
        endTransaction();
}

QModelIndex GraphsModel::extractSubGraph(const QModelIndexList& nodes, const QModelIndex& fromSubgIdx, const QString& toSubg, bool enableTrans)
{
    if (nodes.isEmpty() || !fromSubgIdx.isValid() || toSubg.isEmpty() || subGraph(toSubg))
    {
        return QModelIndex();
    }

    enableTrans = true;    //dangerous to trans...
    if (enableTrans)
        beginTransaction("extract a new graph");

    //first, new the target subgraph
    newSubgraph(toSubg);
    QModelIndex toSubgIdx = index(toSubg);

    //copy nodes to new subg.
    copyPaste(fromSubgIdx, nodes, toSubgIdx, QPointF(0, 0), enableTrans);

    //remove nodes from old subg.
    QStringList ids;
    for (QModelIndex idx : nodes)
        ids.push_back(idx.data(ROLE_OBJID).toString());
    for (QString id : ids)
        removeNode(id, fromSubgIdx, enableTrans);

    if (enableTrans)
        endTransaction();

    return toSubgIdx;
}

bool GraphsModel::IsSubGraphNode(const QModelIndex& nodeIdx) const
{
    if (!nodeIdx.isValid())
        return false;

    QString nodeName = nodeIdx.data(ROLE_OBJNAME).toString();
    return subGraph(nodeName) != nullptr;
}

void GraphsModel::removeNode(int row, const QModelIndex& subGpIdx)
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph);
	if (pGraph)
	{
        QModelIndex idx = pGraph->index(row, 0);
        pGraph->removeNode(row);
	}
}


void GraphsModel::removeLinks(const QList<QPersistentModelIndex>& info, const QModelIndex& subGpIdx, bool enableTransaction)
{
    for (const QPersistentModelIndex& linkIdx : info)
    {
        removeLink(linkIdx, subGpIdx, enableTransaction);
    }
}

void GraphsModel::removeLink(const QPersistentModelIndex& linkIdx, const QModelIndex& subGpIdx, bool enableTransaction)
{
    if (!linkIdx.isValid())
        return;

    if (enableTransaction)
    {
		RemoveLinkCommand* pCmd = new RemoveLinkCommand(linkIdx, this, subGpIdx);
		m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

		SubGraphModel* pGraph = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pGraph && linkIdx.isValid());

        const QString& outNode = linkIdx.data(ROLE_OUTNODE).toString();
        const QString& outSock = linkIdx.data(ROLE_OUTSOCK).toString();
        const QString& inNode = linkIdx.data(ROLE_INNODE).toString();
        const QString& inSock = linkIdx.data(ROLE_INSOCK).toString();

        const QModelIndex& outIdx = pGraph->index(outNode);
        const QModelIndex& inIdx = pGraph->index(inNode);

        OUTPUT_SOCKETS outputs = pGraph->data(outIdx, ROLE_OUTPUTS).value<OUTPUT_SOCKETS>();
        if (outputs.find(outSock) != outputs.end())
        {
            outputs[outSock].linkIndice.removeOne(linkIdx);
            pGraph->setData(outIdx, QVariant::fromValue(outputs), ROLE_OUTPUTS);
        }

        INPUT_SOCKETS inputs = pGraph->data(inIdx, ROLE_INPUTS).value<INPUT_SOCKETS>();
        if (inputs.find(inSock) != inputs.end())
        {
            inputs[inSock].linkIndice.removeOne(linkIdx);
            pGraph->setData(inIdx, QVariant::fromValue(inputs), ROLE_INPUTS);
        }

		m_linkModel->removeRow(linkIdx.row());
    }
}

QModelIndex GraphsModel::addLink(const EdgeInfo& info, const QModelIndex& subGpIdx, bool bAddDynamicSock, bool enableTransaction)
{
    if (enableTransaction)
    {
        beginTransaction("addLink issues");
        zeno::scope_exit sp([=]() { endTransaction(); });

        AddLinkCommand* pCmd = new AddLinkCommand(info, this, subGpIdx);
        m_stack->push(pCmd);
        return QModelIndex();
    }
    else
    {
        ApiLevelScope batch(this);

		SubGraphModel* pGraph = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pGraph, QModelIndex());

        QStandardItem* pItem = new QStandardItem;
        pItem->setData(UiHelper::generateUuid(), ROLE_OBJID);
        pItem->setData(info.inputNode, ROLE_INNODE);
        pItem->setData(info.inputSock, ROLE_INSOCK);
        pItem->setData(info.outputNode, ROLE_OUTNODE);
        pItem->setData(info.outputSock, ROLE_OUTSOCK);

        ZASSERT_EXIT(pGraph->index(info.inputNode).isValid() && pGraph->index(info.outputNode).isValid(), QModelIndex());

        m_linkModel->appendRow(pItem);
        QModelIndex linkIdx = m_linkModel->indexFromItem(pItem);

        const QModelIndex& inIdx = pGraph->index(info.inputNode);
        const QModelIndex& outIdx = pGraph->index(info.outputNode);

        INPUT_SOCKETS inputs = inIdx.data(ROLE_INPUTS).value<INPUT_SOCKETS>();
        OUTPUT_SOCKETS outputs = outIdx.data(ROLE_OUTPUTS).value<OUTPUT_SOCKETS>();
        inputs[info.inputSock].linkIndice.append(QPersistentModelIndex(linkIdx));
        outputs[info.outputSock].linkIndice.append(QPersistentModelIndex(linkIdx));
        pGraph->setData(inIdx, QVariant::fromValue(inputs), ROLE_INPUTS);
        pGraph->setData(outIdx, QVariant::fromValue(outputs), ROLE_OUTPUTS);

        //todo: encapsulation when case grows.
        if (bAddDynamicSock)
        {
            const QString& inNodeName = inIdx.data(ROLE_OBJNAME).toString();
            const QString& outNodeName = outIdx.data(ROLE_OBJNAME).toString();
            if (inNodeName == "MakeList" || inNodeName == "MakeDict")
            {
                const INPUT_SOCKETS _inputs = inIdx.data(ROLE_INPUTS).value<INPUT_SOCKETS>();
                QList<QString> lst = _inputs.keys();
                int maxObjId = UiHelper::getMaxObjId(lst);
                if (maxObjId == -1)
                    maxObjId = 0;
                QString maxObjSock = QString("obj%1").arg(maxObjId);
                QString lastKey = inputs.lastKey();
                if (info.inputSock == lastKey)
                {
                    //add a new
                    const QString &newObjName = QString("obj%1").arg(maxObjId + 1);
                    INPUT_SOCKET inSockObj;
                    inSockObj.info.name = newObjName;

                    //need transcation.
                    SOCKET_UPDATE_INFO sockUpdateinfo;
                    sockUpdateinfo.bInput = true;
                    sockUpdateinfo.updateWay = SOCKET_INSERT;
                    sockUpdateinfo.newInfo.name = newObjName;
                    if (inNodeName == "MakeDict")
                    {
                        sockUpdateinfo.newInfo.control = CONTROL_DICTKEY;
                    }
                    updateSocket(info.inputNode, sockUpdateinfo, subGpIdx, false);
                }
            }
            if (outNodeName == "ExtractDict")
            {
                QList<QString> lst = outputs.keys();
                int maxObjId = UiHelper::getMaxObjId(lst);
                if (maxObjId == -1)
                    maxObjId = 0;
                QString maxObjSock = QString("obj%1").arg(maxObjId);
                QString lastKey = outputs.lastKey();
                if (info.outputSock == lastKey)
                {
                    //add a new
                    const QString &newObjName = QString("obj%1").arg(maxObjId + 1);
                    OUTPUT_SOCKET outSockObj;
                    outSockObj.info.name = newObjName;

                    SOCKET_UPDATE_INFO sockUpdateinfo;
                    sockUpdateinfo.bInput = false;
                    sockUpdateinfo.updateWay = SOCKET_INSERT;
                    sockUpdateinfo.newInfo.name = newObjName;
                    sockUpdateinfo.newInfo.control = CONTROL_DICTKEY;
                    updateSocket(info.outputNode, sockUpdateinfo, subGpIdx, false);
                }
            }
        }
        return linkIdx;
    }
}

void GraphsModel::updateLinkInfo(const QPersistentModelIndex& linkIdx, const LINK_UPDATE_INFO& info, bool enableTransaction)
{
    if (enableTransaction)
    {

    }
    else
    {
        m_linkModel->setData(linkIdx, info.newEdge.inputNode, ROLE_INNODE);
        m_linkModel->setData(linkIdx, info.newEdge.inputSock, ROLE_INSOCK);
        m_linkModel->setData(linkIdx, info.newEdge.outputNode, ROLE_OUTNODE);
        m_linkModel->setData(linkIdx, info.newEdge.outputSock, ROLE_OUTSOCK);
    }
}

void GraphsModel::setIOProcessing(bool bIOProcessing)
{
    m_bIOProcessing = bIOProcessing;
}

bool GraphsModel::IsIOProcessing() const
{
    return m_bIOProcessing;
}

void GraphsModel::removeSubGraph(const QString& name)
{
    if (name.compare("main", Qt::CaseInsensitive) == 0)
        return;

    for (int i = 0; i < m_subGraphs.size(); i++)
    {
        if (m_subGraphs[i]->name() == name)
        {
            removeGraph(i);
        }
        else
        {
            m_subGraphs[i]->removeNodeByDescName(name);
        }
    }
}

void GraphsModel::updateParamInfo(const QString& id, PARAM_UPDATE_INFO info, const QModelIndex& subGpIdx, bool enableTransaction)
{
    if (enableTransaction)
    {
        QModelIndex idx = index(id, subGpIdx);
        const QString& nodeName = idx.data(ROLE_OBJNAME).toString();
        //validate the name of SubInput/SubOutput
        if (info.name == "name" && (nodeName == "SubInput" || nodeName == "SubOutput"))
        {
            const QString& subgName = subGpIdx.data(ROLE_OBJNAME).toString();
            QString correctName = UiHelper::correctSubIOName(this, subgName, info.newValue.toString(), nodeName == "SubInput");
            info.newValue = correctName;
        }

        UpdateDataCommand* pCmd = new UpdateDataCommand(id, info, this, subGpIdx);
        m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

		SubGraphModel* pGraph = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pGraph);
		pGraph->updateParam(id, info.name, info.newValue);

        const QString& nodeName = pGraph->index(id).data(ROLE_OBJNAME).toString();
        if (nodeName == "SubInput" || nodeName == "SubOutput")
        {
            if (info.name == "name")
            {
                SOCKET_UPDATE_INFO updateInfo;
                updateInfo.bInput = (nodeName == "SubInput");
                updateInfo.oldInfo.name = info.oldValue.toString();
                updateInfo.newInfo.name = info.newValue.toString();

                updateInfo.updateWay = SOCKET_UPDATE_NAME;
                updateDescInfo(pGraph->name(), updateInfo);
            }
            else
            {
                const QString& subName = pGraph->getParamValue(id, "name").toString();
                const QVariant &deflVal = pGraph->getParamValue(id, "defl");
                SOCKET_UPDATE_INFO updateInfo;
                updateInfo.newInfo.name = subName;
                updateInfo.oldInfo.name = subName;
                updateInfo.bInput = (nodeName == "SubInput");

                if (info.name == "defl")
                {
                    updateInfo.updateWay = SOCKET_UPDATE_DEFL;
                    updateInfo.oldInfo.type = pGraph->getParamValue(id, "type").toString();
                    updateInfo.oldInfo.control = UiHelper::getControlType(updateInfo.oldInfo.type);
                    updateInfo.newInfo.control = updateInfo.oldInfo.control;
                    updateInfo.oldInfo.defaultValue = info.oldValue;
                    updateInfo.newInfo.defaultValue = info.newValue;
                    updateDescInfo(pGraph->name(), updateInfo);
                }
                else if (info.name == "type")
                {
                    pGraph->updateParam(id, "type", info.newValue);

                    updateInfo.updateWay = SOCKET_UPDATE_TYPE;
                    updateInfo.oldInfo.type = info.oldValue.toString();
                    updateInfo.newInfo.type = info.newValue.toString();
                    updateInfo.newInfo.defaultValue = UiHelper::initDefaultValue(updateInfo.newInfo.type);

                    //update defl type and value on SubInput/SubOutput, when type changes.
                    pGraph->updateParam(id, "defl", updateInfo.newInfo.defaultValue, &updateInfo.newInfo.type);

                    updateInfo.oldInfo.control = UiHelper::getControlType(updateInfo.oldInfo.type);
                    updateInfo.newInfo.control = UiHelper::getControlType(updateInfo.newInfo.type);
                    updateDescInfo(pGraph->name(), updateInfo);
                }
            }
        }
    }
}

void GraphsModel::updateParamNotDesc(const QString &id, PARAM_UPDATE_INFO info, const QModelIndex &subGpIdx,
    bool enableTransaction)
{
    ApiLevelScope batch(this);
    SubGraphModel *pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph);
    pGraph->updateParamNotDesc(id, info.name, info.newValue);
}

void GraphsModel::updateSocket(const QString& nodeid, SOCKET_UPDATE_INFO info, const QModelIndex& subGpIdx, bool enableTransaction)
{
    if (enableTransaction)
    {
        UpdateSocketCommand* pCmd = new UpdateSocketCommand(nodeid, info, this, subGpIdx);
        m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

		SubGraphModel* pSubg = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pSubg);
		pSubg->updateSocket(nodeid, info);
    }
}

void GraphsModel::updateSocketDefl(const QString& id, PARAM_UPDATE_INFO info, const QModelIndex& subGpIdx, bool enableTransaction)
{
    if (enableTransaction)
    {
        UpdateSockDeflCommand* pCmd = new UpdateSockDeflCommand(id, info, this, subGpIdx);
        m_stack->push(pCmd);
    }
    else
    {
        ApiLevelScope batch(this);

        SubGraphModel *pSubg = subGraph(subGpIdx.row());
        ZASSERT_EXIT(pSubg);
        pSubg->updateSocketDefl(id, info);
    }
}

void GraphsModel::updateNodeStatus(const QString& nodeid, STATUS_UPDATE_INFO info, const QModelIndex& subgIdx, bool enableTransaction)
{
    if (enableTransaction)
    {
        UpdateStateCommand* pCmd = new UpdateStateCommand(nodeid, info, this, subgIdx);
        m_stack->push(pCmd);
    }
    else
    {
        SubGraphModel *pSubg = subGraph(subgIdx.row());
        ZASSERT_EXIT(pSubg);
        if (info.role != ROLE_OBJPOS && info.role != ROLE_COLLASPED)
        {
            ApiLevelScope batch(this);
            pSubg->updateNodeStatus(nodeid, info);
        }
        else
        {
            pSubg->updateNodeStatus(nodeid, info);
        }
    }
}

void GraphsModel::updateBlackboard(const QString& id, const BLACKBOARD_INFO& newInfo, const QModelIndex& subgIdx, bool enableTransaction)
{
    SubGraphModel *pSubg = subGraph(subgIdx.row());
    const QModelIndex& idx = pSubg->index(id);
    ZASSERT_EXIT(pSubg);

    if (enableTransaction)
    {
        PARAMS_INFO params = idx.data(ROLE_PARAMS_NO_DESC).value<PARAMS_INFO>();
        BLACKBOARD_INFO oldInfo = params["blackboard"].value.value<BLACKBOARD_INFO>();
        UpdateBlackboardCommand *pCmd = new UpdateBlackboardCommand(id, newInfo, oldInfo, this, subgIdx);
        m_stack->push(pCmd);
    }
    else
    {
        PARAMS_INFO params = idx.data(ROLE_PARAMS_NO_DESC).value<PARAMS_INFO>();
        params["blackboard"].name = "blackboard";
        params["blackboard"].value = QVariant::fromValue(newInfo);
        pSubg->setData(idx, QVariant::fromValue(params), ROLE_PARAMS_NO_DESC);
    }
}

bool GraphsModel::updateSocketNameNotDesc(const QString& id, SOCKET_UPDATE_INFO info, const QModelIndex& subGpIdx, bool enableTransaction)
{
    SubGraphModel* pSubg = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pSubg, false);
    const QModelIndex &idx = pSubg->index(id);

    bool ret = false;
    if (enableTransaction)
    {
        UpdateNotDescSockNameCommand *pCmd = new UpdateNotDescSockNameCommand(id, info, this, subGpIdx);
        m_stack->push(pCmd);

        ret = m_retStack.top();
        m_retStack.pop();
        return ret;
    }
    else
    {
        if (info.updateWay == SOCKET_UPDATE_NAME)
        {
            //especially for MakeDict，we update the name of socket which are not registerd by descriptors.
            const QString& newSockName = info.newInfo.name;
            const QString& oldSockName = info.oldInfo.name;

            INPUT_SOCKETS inputs = pSubg->data(idx, ROLE_INPUTS).value<INPUT_SOCKETS>();
            if (info.bInput && newSockName != oldSockName && inputs.find(newSockName) == inputs.end())
            {
                auto iter = inputs.find(oldSockName);
                ZASSERT_EXIT(iter != inputs.end(), false);

                iter->first = newSockName;
                iter->second.info.name = newSockName;

                //update all link connect with oldInfo.
                m_linkModel->blockSignals(true);
                for (auto idx : iter->second.linkIndice)
                {
                    m_linkModel->setData(idx, newSockName, ROLE_INSOCK);
                }
                m_linkModel->blockSignals(false);

                pSubg->setData(idx, QVariant::fromValue(inputs), ROLE_INPUTS);
                ret = true;
            }

            OUTPUT_SOCKETS outputs = pSubg->data(idx, ROLE_OUTPUTS).value<OUTPUT_SOCKETS>();
            if (!info.bInput && newSockName != oldSockName && outputs.find(newSockName) == outputs.end())
            {
                OUTPUT_SOCKET& newOutput = outputs[newSockName];
                const OUTPUT_SOCKET& oldOutput = outputs[oldSockName];
                newOutput = oldOutput;
                newOutput.info.name = newSockName;

                //update all link connect with oldInfo
                m_linkModel->blockSignals(true);
                for (auto idx : oldOutput.linkIndice)
                {
                    m_linkModel->setData(idx, newSockName, ROLE_OUTSOCK);
                }
                m_linkModel->blockSignals(false);

                outputs.remove(oldSockName);
                pSubg->setData(idx, QVariant::fromValue(outputs), ROLE_OUTPUTS);
                ret = true;
            }
        }
        m_retStack.push(ret);
        return ret;
    }
}

void GraphsModel::updateDescInfo(const QString& descName, const SOCKET_UPDATE_INFO& updateInfo)
{
    ZASSERT_EXIT(m_subgsDesc.find(descName) != m_subgsDesc.end());
	NODE_DESC& desc = m_subgsDesc[descName];
	switch (updateInfo.updateWay)
	{
		case SOCKET_INSERT:
		{
            const QString& nameValue = updateInfo.newInfo.name;
            if (updateInfo.bInput)
            {
                // add SubInput
                ZASSERT_EXIT(desc.inputs.find(nameValue) == desc.inputs.end());
                INPUT_SOCKET inputSocket;
                inputSocket.info = updateInfo.newInfo;
                desc.inputs[nameValue] = inputSocket;
            }
            else
            {
                // add SubOutput
                ZASSERT_EXIT(desc.outputs.find(nameValue) == desc.outputs.end());
                OUTPUT_SOCKET outputSocket;
                outputSocket.info = updateInfo.newInfo;
                desc.outputs[nameValue] = outputSocket;
            }
            break;
        }
        case SOCKET_REMOVE:
        {
            const QString& nameValue = updateInfo.newInfo.name;
            if (updateInfo.bInput)
            {
                ZASSERT_EXIT(desc.inputs.find(nameValue) != desc.inputs.end());
                desc.inputs.remove(nameValue);
            }
            else
            {
                ZASSERT_EXIT(desc.outputs.find(nameValue) != desc.outputs.end());
                desc.outputs.remove(nameValue);
            }
            break;
        }
        case SOCKET_UPDATE_NAME:
        {
            const QString& oldName = updateInfo.oldInfo.name;
            const QString& newName = updateInfo.newInfo.name;
            if (updateInfo.bInput)
            {
                ZASSERT_EXIT(desc.inputs.find(oldName) != desc.inputs.end() &&
                    desc.inputs.find(newName) == desc.inputs.end());
                desc.inputs[newName] = desc.inputs[oldName];
                desc.inputs[newName].info.name = newName;
                desc.inputs.remove(oldName);
            }
            else
            {
                ZASSERT_EXIT(desc.outputs.find(oldName) != desc.outputs.end() &&
                    desc.outputs.find(newName) == desc.outputs.end());
                desc.outputs[newName] = desc.outputs[oldName];
                desc.outputs[newName].info.name = newName;
                desc.outputs.remove(oldName);
            }
            break;
        }
        case SOCKET_UPDATE_DEFL:
        {
            const QString& name = updateInfo.newInfo.name;
            if (updateInfo.bInput)
			{
				ZASSERT_EXIT(desc.inputs.find(name) != desc.inputs.end());
				desc.inputs[name].info.defaultValue = updateInfo.newInfo.defaultValue;
            }
            else
            {
                ZASSERT_EXIT(desc.outputs.find(name) != desc.outputs.end());
                desc.outputs[name].info.defaultValue = updateInfo.newInfo.defaultValue;
            }
            break;   // if break, the default value will be sync to all subnet nodes. otherwise(if return), the value will not sync to outside subnet nodes.
        }
        case SOCKET_UPDATE_TYPE:
        {
            const QString& name = updateInfo.newInfo.name;
            const QString& socketType = updateInfo.newInfo.type;
            PARAM_CONTROL ctrl = UiHelper::getControlType(socketType);
            if (updateInfo.bInput)
            {
                ZASSERT_EXIT(desc.inputs.find(name) != desc.inputs.end());
                desc.inputs[name].info.type = socketType;
                desc.inputs[name].info.control = ctrl;
                desc.inputs[name].info.defaultValue = updateInfo.newInfo.defaultValue;
            }
            else
            {
                ZASSERT_EXIT(desc.outputs.find(name) != desc.outputs.end());
                desc.outputs[name].info.type = socketType;
                desc.outputs[name].info.control = ctrl;
                desc.outputs[name].info.defaultValue = updateInfo.newInfo.defaultValue;
            }
            break;
        }
    }

    for (int i = 0; i < m_subGraphs.size(); i++)
    {
        SubGraphModel* pModel = m_subGraphs[i];
        if (pModel->name() != descName)
        {
            QModelIndexList results = pModel->match(index(0, 0), ROLE_OBJNAME, descName, -1, Qt::MatchExactly);
            for (auto idx : results)
            {
                const QString& nodeId = idx.data(ROLE_OBJID).toString();
                updateSocket(nodeId, updateInfo, index(i, 0), false);
            }
        }
    }
}

NODE_DATA GraphsModel::itemData(const QModelIndex& index, const QModelIndex& subGpIdx) const
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph, NODE_DATA());
    return pGraph->itemData(index);
}

void GraphsModel::setName(const QString& name, const QModelIndex& subGpIdx)
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph);
    pGraph->setName(name);
}

void GraphsModel::clearSubGraph(const QModelIndex& subGpIdx)
{
	SubGraphModel* pGraph = subGraph(subGpIdx.row());
    ZASSERT_EXIT(pGraph);
    pGraph->clear();
}

void GraphsModel::clear()
{
    for (int r = 0; r < this->rowCount(); r++)
    {
        const QModelIndex& subgIdx = this->index(r, 0);
        clearSubGraph(subgIdx);
    }
    m_linkModel->clear();
    emit modelClear();
}

QModelIndexList GraphsModel::searchInSubgraph(const QString& objName, const QModelIndex& subgIdx)
{
    SubGraphModel* pModel = subGraph(subgIdx.row());
    QModelIndexList list;
    auto count = pModel->rowCount();

    for (auto i = 0; i < count; i++) {
        auto index = pModel->index(i, 0);
        auto item = pModel->itemData(index);
        if (item[ROLE_OBJID].toString().contains(objName, Qt::CaseInsensitive)) {
            list.append(index);
        }
        else {
            QString _type("string");
            bool inserted = false;
            {
                auto params = item[ROLE_PARAMETERS].value<PARAMS_INFO>();
                auto iter = params.begin();
                while (iter != params.end()) {
                    if (iter.value().typeDesc == _type) {
                        if (iter.value().value.toString().contains(objName, Qt::CaseInsensitive)) {
                            list.append(index);
                            inserted = true;
                            break;
                        }
                    }
                    ++iter;
                }
            }
            if (inserted) {
                continue;
            }
            {
                auto inputs = item[ROLE_INPUTS].value<INPUT_SOCKETS>();
                auto iter = inputs.begin();
                while (iter != inputs.end()) {
                    if (iter->value().info.type == _type) {
                        if (iter->value().info.defaultValue.toString().contains(objName, Qt::CaseInsensitive)) {
                            list.append(index);
                            inserted = true;
                            break;
                        }
                    }
                    ++iter;
                }

            }
        }
    }
    return list;
}

QModelIndexList GraphsModel::subgraphsIndice() const
{
    return persistentIndexList();
}

QStandardItemModel* GraphsModel::linkModel() const
{
    return m_linkModel;
}

void GraphsModel::on_subg_dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles)
{
    SubGraphModel* pSubModel = qobject_cast<SubGraphModel*>(sender());

    ZASSERT_EXIT(pSubModel && roles.size() == 1);
    QModelIndex subgIdx = indexBySubModel(pSubModel);
    emit _dataChanged(subgIdx, topLeft, roles[0]);
}

void GraphsModel::on_subg_rowsAboutToBeInserted(const QModelIndex& parent, int first, int last)
{
    SubGraphModel* pSubModel = qobject_cast<SubGraphModel*>(sender());

    ZASSERT_EXIT(pSubModel);
    QModelIndex subgIdx = indexBySubModel(pSubModel);
    emit _rowsAboutToBeInserted(subgIdx, first, last);
}

void GraphsModel::on_subg_rowsInserted(const QModelIndex& parent, int first, int last)
{
    SubGraphModel* pSubModel = qobject_cast<SubGraphModel*>(sender());
    ZASSERT_EXIT(pSubModel);
    QModelIndex subgIdx = indexBySubModel(pSubModel);
    emit _rowsInserted(subgIdx, parent, first, last);
}

void GraphsModel::on_subg_rowsAboutToBeRemoved(const QModelIndex& parent, int first, int last)
{
    SubGraphModel* pSubModel = qobject_cast<SubGraphModel*>(sender());
    ZASSERT_EXIT(pSubModel);
    QModelIndex subgIdx = indexBySubModel(pSubModel);
    emit _rowsAboutToBeRemoved(subgIdx, parent, first, last);
}

void GraphsModel::on_subg_rowsRemoved(const QModelIndex& parent, int first, int last)
{
    SubGraphModel* pSubModel = qobject_cast<SubGraphModel*>(sender());

    ZASSERT_EXIT(pSubModel);
    QModelIndex subgIdx = indexBySubModel(pSubModel);
    emit _rowsRemoved(parent, first, last);
}

QModelIndex GraphsModel::getSubgraphIndex(const QModelIndex& linkIdx)
{
	const QString& inNode = linkIdx.data(ROLE_INNODE).toString();
    for (int r = 0; r < m_subGraphs.size(); r++)
    {
        SubGraphModel* pSubModel = m_subGraphs[r];
		if (pSubModel->index(inNode).isValid())
		{
            return index(r, 0);
		}
    }
    return QModelIndex();
}

QRectF GraphsModel::viewRect(const QModelIndex& subgIdx)
{
	SubGraphModel* pModel = subGraph(subgIdx.row());
    ZASSERT_EXIT(pModel, QRectF());
    return pModel->viewRect();
}

void GraphsModel::on_linkDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles)
{
	const QModelIndex& subgIdx = getSubgraphIndex(topLeft);
    if (subgIdx.isValid())
	    emit linkDataChanged(subgIdx, topLeft, roles[0]);
}

void GraphsModel::on_linkAboutToBeInserted(const QModelIndex& parent, int first, int last)
{
     QModelIndex linkIdx = m_linkModel->index(first, 0, parent);
     const QModelIndex& subgIdx = getSubgraphIndex(linkIdx);
     if (subgIdx.isValid())
        emit linkAboutToBeInserted(subgIdx, parent, first, last);
}

void GraphsModel::on_linkInserted(const QModelIndex& parent, int first, int last)
{
	QModelIndex linkIdx = m_linkModel->index(first, 0, parent);
	const QModelIndex& subgIdx = getSubgraphIndex(linkIdx);
    if (subgIdx.isValid())
	    emit linkInserted(subgIdx, parent, first, last);
}

void GraphsModel::on_linkAboutToBeRemoved(const QModelIndex& parent, int first, int last)
{
	QModelIndex linkIdx = m_linkModel->index(first, 0, parent);
	const QModelIndex& subgIdx = getSubgraphIndex(linkIdx);
    if (subgIdx.isValid())
	    emit linkAboutToBeRemoved(subgIdx, parent, first, last);
}

void GraphsModel::on_linkRemoved(const QModelIndex& parent, int first, int last)
{
	QModelIndex linkIdx = m_linkModel->index(first, 0, parent);
	const QModelIndex& subgIdx = getSubgraphIndex(linkIdx);
    if (subgIdx.isValid())
	    emit linkRemoved(subgIdx, parent, first, last);
}


void GraphsModel::onSubIOAddRemove(SubGraphModel* pSubModel, const QModelIndex& idx, bool bInput, bool bInsert)
{
    const QString& objId = idx.data(ROLE_OBJID).toString();
    const QString& objName = idx.data(ROLE_OBJNAME).toString();

    PARAMS_INFO params = idx.data(ROLE_PARAMETERS).value<PARAMS_INFO>();
    ZASSERT_EXIT(params.find("name") != params.end() &&
                 params.find("type") != params.end() &&
                 params.find("defl") != params.end());

    const QString& nameValue = params["name"].value.toString();
    const QString& typeValue = params["type"].value.toString();
    QVariant deflVal = params["defl"].value;
    PARAM_CONTROL ctrl = UiHelper::getControlType(typeValue);

    SOCKET_UPDATE_INFO updateInfo;
    updateInfo.bInput = bInput;
    updateInfo.updateWay = bInsert ? SOCKET_INSERT : SOCKET_REMOVE;

    SOCKET_INFO newInfo("", nameValue, ctrl, typeValue, deflVal);
    updateInfo.newInfo = newInfo;

    const QString& subnetNodeName = pSubModel->name();
    updateDescInfo(subnetNodeName, updateInfo);
}

QList<SEARCH_RESULT> GraphsModel::search(const QString& content, int searchOpts)
{
    QList<SEARCH_RESULT> results;
    if (content.isEmpty())
        return results;

    if (searchOpts & SEARCH_SUBNET)
    {
        QModelIndexList lst = match(index(0, 0), ROLE_OBJNAME, content, -1, Qt::MatchContains);
        for (QModelIndex subgIdx : lst)
        {
            SEARCH_RESULT result;
            result.targetIdx = subgIdx;
            result.type = SEARCH_SUBNET;
            results.append(result);
        }
    }
    if (searchOpts & SEARCH_NODECLS)
    {
        for (auto subgInfo : m_subGraphs)
        {
            SubGraphModel* pModel = subgInfo;
            QModelIndex subgIdx = indexBySubModel(pModel);
            //todo: searching by key.
            QModelIndexList lst = pModel->match(pModel->index(0, 0), ROLE_OBJNAME, content, -1, Qt::MatchContains);
            for (QModelIndex nodeIdx : lst)
            {
                SEARCH_RESULT result;
                result.targetIdx = nodeIdx;
                result.subgIdx = subgIdx;
                result.type = SEARCH_NODECLS;
                results.append(result);
            }
        }
    }
    if (searchOpts & SEARCH_NODEID)
    {
        for (auto subgInfo : m_subGraphs)
        {
            SubGraphModel* pModel = subgInfo;
            QModelIndex subgIdx = indexBySubModel(pModel);
            QModelIndexList lst = pModel->match(pModel->index(0, 0), ROLE_OBJID, content, -1, Qt::MatchContains);
            if (!lst.isEmpty())
            {
                const QModelIndex &nodeIdx = lst[0];

                SEARCH_RESULT result;
                result.targetIdx = nodeIdx;
                result.subgIdx = subgIdx;
                result.type = SEARCH_NODEID;
                results.append(result);
                break;
            }
        }
    }

    return results;
}

void GraphsModel::collaspe(const QModelIndex& subgIdx)
{
	SubGraphModel* pModel = subGraph(subgIdx.row());
    ZASSERT_EXIT(pModel);
    pModel->collaspe();
}

void GraphsModel::expand(const QModelIndex& subgIdx)
{
	SubGraphModel* pModel = subGraph(subgIdx.row());
    ZASSERT_EXIT(pModel);
    pModel->expand();
}

bool GraphsModel::hasDescriptor(const QString& nodeName) const
{
    return m_nodesDesc.find(nodeName) != m_nodesDesc.end() ||
        m_subgsDesc.find(nodeName) != m_subgsDesc.end();
}

void GraphsModel::resolveLinks(const QModelIndex& idx, SubGraphModel* pCurrentGraph)
{
    ZASSERT_EXIT(pCurrentGraph);

    const QString& inNode = idx.data(ROLE_OBJID).toString();
	INPUT_SOCKETS inputs = idx.data(ROLE_INPUTS).value<INPUT_SOCKETS>();
	foreach(const QString & inSockName, inputs.keys())
	{
		const INPUT_SOCKET& inSocket = inputs[inSockName];
		//init connection
		for (const QString& outNode : inSocket.outNodes.keys())
		{
			const QModelIndex& outIdx = pCurrentGraph->index(outNode);
			if (outIdx.isValid())
			{
				//the items in outputs are descripted by core descriptors.
				OUTPUT_SOCKETS outputs = outIdx.data(ROLE_OUTPUTS).value<OUTPUT_SOCKETS>();
				for (const QString &outSock : inSocket.outNodes[outNode].keys())
				{
					//checkout whether outSock is existed.
					if (outputs.find(outSock) == outputs.end())
					{
						const QString& nodeName = outIdx.data(ROLE_OBJNAME).toString();
						zeno::log_warn("no such output socket {} in {}", outSock.toStdString(), nodeName.toStdString());
						continue;
					}
					GraphsModel* pGraphsModel = pCurrentGraph->getGraphsModel();
					const QModelIndex& subgIdx = pGraphsModel->indexBySubModel(pCurrentGraph);
					pGraphsModel->addLink(EdgeInfo(outNode, inNode, outSock, inSockName), subgIdx, false);
				}
			}
		}
	}
}

void GraphsModel::setNodeData(const QModelIndex &nodeIndex, const QModelIndex &subGpIdx, const QVariant &value, int role) {
    SubGraphModel* pModel = this->subGraph(subGpIdx.row());
    ZASSERT_EXIT(pModel);
    pModel->setData(nodeIndex, value, role);
}
