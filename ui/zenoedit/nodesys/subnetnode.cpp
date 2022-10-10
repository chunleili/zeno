#include "subnetnode.h"
#include "zenoapplication.h"
#include <zenomodel/include/graphsmanagment.h>
#include <zenomodel/include/uihelper.h>
#include "util/log.h"


class TypeValidator : public QValidator
{
public:
    explicit TypeValidator(QObject *parent = nullptr) : QValidator(parent) {

    }
    QValidator::State validate(QString& input, int&) const override
    {
        if (input.isEmpty())
        {
            return Acceptable;
        }
        if (input == "int" ||
            input == "string" ||
            input == "float" ||
            input == "bool" ||
            input == "vec3f" ||
            input == "vec3i" ||
            input == "curve" ||
            input == "heatmap")
        {
            return Acceptable;
        }
        else
        {
            return Intermediate;
        }
    }
    void fixup(QString& str) const override
    {
        str = "";
    }
};


SubnetNode::SubnetNode(bool bInput, const NodeUtilParam& params, QGraphicsItem* parent)
    : ZenoNode(params, parent)
    , m_bInput(bInput)
{

}

SubnetNode::~SubnetNode()
{

}

void SubnetNode::onParamEditFinished(const QString& paramName, const QVariant& textValue)
{
    //get old name first.
    IGraphsModel* pModel = zenoApp->graphsManagment()->currentModel();
    ZASSERT_EXIT(pModel);
    const QString& nodeid = nodeId();
    QModelIndex subgIdx = this->subGraphIndex();
    const PARAMS_INFO& params = index().data(ROLE_PARAMETERS).value<PARAMS_INFO>();
    const QString& oldName = params["name"].value.toString();
    const QString& subnetName = subgIdx.data(ROLE_OBJNAME).toString();
    if (oldName == textValue)
        return;

    ZenoNode::onParamEditFinished(paramName, textValue);
}

QValidator* SubnetNode::validateForParams(PARAM_INFO info)
{
    return ZenoNode::validateForParams(info);
}
