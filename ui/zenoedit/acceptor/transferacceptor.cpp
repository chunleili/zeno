#include <QtWidgets>
#include <zenomodel/include/modelrole.h>
#include <zenomodel/include/igraphsmodel.h>
#include "transferacceptor.h"
#include <zeno/utils/logger.h>
#include "util/log.h"
#include <zenoio/reader/zsgreader.h>
#include <zenomodel/include/nodesmgr.h>
#include <zenomodel/include/uihelper.h>


TransferAcceptor::TransferAcceptor(IGraphsModel* pModel)
    : m_pModel(pModel)
{

}

bool TransferAcceptor::setLegacyDescs(const rapidjson::Value& graphObj, const NODE_DESCS& legacyDescs)
{
    return false;
}

void TransferAcceptor::BeginSubgraph(const QString &name)
{
    //no cache, for data consistency.
    m_links.clear();
    m_nodes.clear();
}

void TransferAcceptor::EndSubgraph()
{

}

bool TransferAcceptor::setCurrentSubGraph(IGraphsModel* pModel, const QModelIndex& subgIdx)
{
    return true;
}

void TransferAcceptor::setFilePath(const QString& fileName)
{

}

void TransferAcceptor::switchSubGraph(const QString& graphName)
{

}

bool TransferAcceptor::addNode(const QString& nodeid, const QString& name, const NODE_DESCS& descriptors)
{
    if (m_nodes.find(nodeid) != m_nodes.end())
        return false;

    NODE_DATA data;
    data[ROLE_OBJID] = nodeid;
    data[ROLE_OBJNAME] = name;
    data[ROLE_COLLASPED] = false;
    data[ROLE_NODETYPE] = NodesMgr::nodeType(name);

    m_nodes.insert(nodeid, data);
    return true;
}

void TransferAcceptor::setViewRect(const QRectF& rc)
{

}

void TransferAcceptor::setSocketKeys(const QString& id, const QStringList& keys)
{
    ZASSERT_EXIT(m_nodes.find(id) == m_nodes.end());
    NODE_DATA& data = m_nodes[id];
    const QString& nodeName = data[ROLE_OBJNAME].toString();
    if (nodeName == "MakeDict")
    {
        INPUT_SOCKETS inputs = data[ROLE_INPUTS].value<INPUT_SOCKETS>();
        for (auto keyName : keys) {
            addDictKey(id, keyName, true);
        }
    } else if (nodeName == "ExtractDict") {
        OUTPUT_SOCKETS outputs = data[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();
        for (auto keyName : keys) {
            addDictKey(id, keyName, false);
        }
    }
}

void TransferAcceptor::initSockets(const QString& id, const QString& name, const NODE_DESCS& legacyDescs)
{
    NODE_DESC desc;
    bool ret = m_pModel->getDescriptor(name, desc);
    ZASSERT_EXIT(ret);
    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());

    //params
    INPUT_SOCKETS inputs;
    PARAMS_INFO params;
    OUTPUT_SOCKETS outputs;

    for (PARAM_INFO descParam : desc.params)
    {
        PARAM_INFO param;
        param.name = descParam.name;
        param.control = descParam.control;
        param.typeDesc = descParam.typeDesc;
        param.defaultValue = descParam.defaultValue;
        params.insert(param.name, param);
    }
    for (INPUT_SOCKET descInput : desc.inputs)
    {
        INPUT_SOCKET input;
        input.info.nodeid = id;
        input.info.control = descInput.info.control;
        input.info.type = descInput.info.type;
        input.info.name = descInput.info.name;
        input.info.defaultValue = descInput.info.defaultValue;
        inputs.insert(input.info.name, input);
    }
    for (OUTPUT_SOCKET descOutput : desc.outputs)
    {
        OUTPUT_SOCKET output;
        output.info.nodeid = id;
        output.info.control = descOutput.info.control;
        output.info.type = descOutput.info.type;
        output.info.name = descOutput.info.name;
        outputs[output.info.name] = output;
    }

    NODE_DATA& data = m_nodes[id];
    data[ROLE_INPUTS] = QVariant::fromValue(inputs);
    data[ROLE_OUTPUTS] = QVariant::fromValue(outputs);
    data[ROLE_PARAMETERS] = QVariant::fromValue(params);
}

void TransferAcceptor::addDictKey(const QString& id, const QString& keyName, bool bInput)
{
    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());

    NODE_DATA &data = m_nodes[id];
    if (bInput)
    {
        INPUT_SOCKETS inputs = data[ROLE_INPUTS].value<INPUT_SOCKETS>();
        if (inputs.find(keyName) == inputs.end())
        {
            INPUT_SOCKET inputSocket;
            inputSocket.info.name = keyName;
            inputSocket.info.nodeid = id;
            inputSocket.info.control = CONTROL_DICTKEY;
            inputSocket.info.type = "";
            inputs[keyName] = inputSocket;
            data[ROLE_INPUTS] = QVariant::fromValue(inputs);
        }
    }
    else
    {
        OUTPUT_SOCKETS outputs = data[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();
        if (outputs.find(keyName) == outputs.end())
        {
            OUTPUT_SOCKET outputSocket;
            outputSocket.info.name = keyName;
            outputSocket.info.nodeid = id;
            outputSocket.info.control = CONTROL_DICTKEY;
            outputSocket.info.type = "";
            outputs[keyName] = outputSocket;
            data[ROLE_OUTPUTS] = QVariant::fromValue(outputs);
        }
    }
}

void TransferAcceptor::setInputSocket(
                const QString &nodeCls,
                const QString &id,
                const QString &inSock,
                const QString &outId,
                const QString &outSock,
                const rapidjson::Value &defaultVal,
                const NODE_DESCS &legacyDescs)
{
    NODE_DESC desc;
    bool ret = m_pModel->getDescriptor(nodeCls, desc);
    ZASSERT_EXIT(ret);

    //parse default value.
    QVariant defaultValue;
    if (!defaultVal.IsNull())
    {
        SOCKET_INFO descInfo;
        if (desc.inputs.find(inSock) != desc.inputs.end()) {
            descInfo = desc.inputs[inSock].info;
        }

        defaultValue = UiHelper::parseJsonByType(descInfo.type, defaultVal, nullptr);
    }

    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());
    NODE_DATA& data = m_nodes[id];

    //standard inputs desc by latest descriptors. 
    INPUT_SOCKETS inputs = data[ROLE_INPUTS].value<INPUT_SOCKETS>();
    if (inputs.find(inSock) != inputs.end())
    {
        if (!defaultValue.isNull())
            inputs[inSock].info.defaultValue = defaultValue;
        if (!outId.isEmpty() && !outSock.isEmpty())
        {
            inputs[inSock].outNodes[outId][outSock] = SOCKET_INFO(outId, outSock);
            EdgeInfo info(outId, id, outSock, inSock);
            m_links.append(info);
        }
        data[ROLE_INPUTS] = QVariant::fromValue(inputs);
    }
    else
    {
        //TODO: optimize the code.
        if (nodeCls == "MakeList" || nodeCls == "MakeDict")
        {
            INPUT_SOCKET inSocket;
            inSocket.info.name = inSock;
            if (nodeCls == "MakeDict")
            {
                inSocket.info.control = CONTROL_DICTKEY;
            }
            inputs[inSock] = inSocket;

            if (!outId.isEmpty() && !outSock.isEmpty())
            {
                inputs[inSock].outNodes[outId][outSock] = SOCKET_INFO(outId, outSock);
                EdgeInfo info(outId, id, outSock, inSock);
                m_links.append(info);
            }
            data[ROLE_INPUTS] = QVariant::fromValue(inputs);
        }
        else
        {
            zeno::log_warn("{}: no such input socket {}", nodeCls.toStdString(), inSock.toStdString());
        }
    }
}

void TransferAcceptor::setParamValue(const QString& id, const QString& nodeCls, const QString& name, const rapidjson::Value& value)
{
    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());
    NODE_DATA& data = m_nodes[id];

    NODE_DESC desc;
    bool ret = m_pModel->getDescriptor(nodeCls, desc);
    ZASSERT_EXIT(ret);

    QVariant var;
    if (!value.IsNull())
    {
        PARAM_INFO paramInfo;
        if (desc.params.find(name) != desc.params.end()) {
            paramInfo = desc.params[name];
        }
        //todo: parentRef;
        if (nodeCls == "SubInput" || nodeCls == "SubOutput")
            var = UiHelper::parseJsonByValue(paramInfo.typeDesc, value, nullptr);
        else
            var = UiHelper::parseJsonByType(paramInfo.typeDesc, value, nullptr);
    }

    PARAMS_INFO params = data[ROLE_PARAMETERS].value<PARAMS_INFO>();
    if (params.find(name) != params.end())
    {
        zeno::log_trace("found param name {}", name.toStdString());
        params[name].value = var;
        data[ROLE_PARAMETERS] = QVariant::fromValue(params);
    }
    else
    {
        PARAMS_INFO _params = data[ROLE_PARAMS_NO_DESC].value<PARAMS_INFO>();
        _params[name].value = var;
        data[ROLE_PARAMS_NO_DESC] = QVariant::fromValue(_params);
        zeno::log_warn("not found param name {}", name.toStdString());
    }
}

void TransferAcceptor::setPos(const QString& id, const QPointF& pos)
{
    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());
    m_nodes[id][ROLE_OBJPOS] = pos;
}

void TransferAcceptor::setOptions(const QString& id, const QStringList& options)
{
    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());
    NODE_DATA &data = m_nodes[id];
    int opts = 0;
    for (int i = 0; i < options.size(); i++)
    {
        const QString& optName = options[i];
        if (optName == "ONCE")
        {
            opts |= OPT_ONCE;
        }
        else if (optName == "PREP")
        {
            opts |= OPT_PREP;
        }
        else if (optName == "VIEW")
        {
            opts |= OPT_VIEW;
        }
        else if (optName == "MUTE")
        {
            opts |= OPT_MUTE;
        }
        else if (optName == "collapsed")
        {
            data[ROLE_COLLASPED] = true;
        }
    }
    data[ROLE_OPTIONS] = opts;
}

void TransferAcceptor::setColorRamps(const QString& id, const COLOR_RAMPS& colorRamps)
{

}

void TransferAcceptor::setBlackboard(const QString& id, const BLACKBOARD_INFO& blackboard)
{
    ZASSERT_EXIT(m_nodes.find(id) != m_nodes.end());
    NODE_DATA &data = m_nodes[id];

    //todO
}

void TransferAcceptor::setTimeInfo(const TIMELINE_INFO& info)
{
}

TIMELINE_INFO TransferAcceptor::timeInfo() const
{
    return TIMELINE_INFO();
}

void TransferAcceptor::setLegacyCurve(
    const QString& id,
    const QVector<QPointF>& pts,
    const QVector<QPair<QPointF, QPointF>>& hdls)
{
}

QObject* TransferAcceptor::currGraphObj()
{
    return nullptr;
}

void TransferAcceptor::endInputs(const QString& id, const QString& nodeCls)
{

}

void TransferAcceptor::endParams(const QString& id, const QString& nodeCls)
{
    if (nodeCls == "SubInput" || nodeCls == "SubOutput")
    {
        NODE_DATA& data = m_nodes[id];
        PARAMS_INFO params = data[ROLE_PARAMETERS].value<PARAMS_INFO>();
        ZASSERT_EXIT(params.find("name") != params.end() &&
            params.find("type") != params.end() &&
            params.find("defl") != params.end());

        const QString& descType = params["type"].value.toString();
        PARAM_INFO& defl = params["defl"];
        defl.control = UiHelper::getControlType(descType);
        defl.value = UiHelper::parseVarByType(descType, defl.value, nullptr);
        defl.typeDesc = descType;
        data[ROLE_PARAMETERS] = QVariant::fromValue(params);
    }
}

QMap<QString, NODE_DATA> TransferAcceptor::nodes() const
{
    return m_nodes;
}

void TransferAcceptor::getDumpData(QMap<QString, NODE_DATA>& nodes, QList<EdgeInfo>& links)
{
    nodes = m_nodes;
    links = m_links;
}

void TransferAcceptor::reAllocIdents()
{
    QMap<QString, QString> old2new;
    QMap<QString, NODE_DATA> newNodes;
    for (QString key : m_nodes.keys())
    {
        const NODE_DATA data = m_nodes[key];
        const QString& oldId = data[ROLE_OBJID].toString();
        const QString& name = data[ROLE_OBJNAME].toString();
        const QString& newId = UiHelper::generateUuid(name);
        NODE_DATA newData = data;
        newData[ROLE_OBJID] = newId;
        newNodes.insert(newId, newData);
        old2new.insert(oldId, newId);
    }
    //replace all the old-id in newNodes.
    for (QString newId : newNodes.keys())
    {
        NODE_DATA& data = newNodes[newId];
        INPUT_SOCKETS inputs = data[ROLE_INPUTS].value<INPUT_SOCKETS>();
        for (INPUT_SOCKET inputSocket : inputs)
        {
            inputSocket.info.nodeid = newId;
        }

        OUTPUT_SOCKETS outputs = data[ROLE_OUTPUTS].value<OUTPUT_SOCKETS>();
        for (OUTPUT_SOCKET outputSocket : outputs)
        {
            outputSocket.info.nodeid = newId;
        }

        data[ROLE_INPUTS] = QVariant::fromValue(inputs);
        data[ROLE_OUTPUTS] = QVariant::fromValue(outputs);
    }

    for (EdgeInfo& link : m_links)
    {
        ZASSERT_EXIT(old2new.find(link.inputNode) != old2new.end() &&
                    old2new.find(link.outputNode) != old2new.end());
        link.inputNode = old2new[link.inputNode];
        link.outputNode = old2new[link.outputNode];
    }

    m_nodes.clear();
    m_nodes = newNodes;
}