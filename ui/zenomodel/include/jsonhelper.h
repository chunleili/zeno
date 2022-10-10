#ifndef __JSON_HELPER_H__
#define __JSON_HELPER_H__

#include <QObject>
#include <QtWidgets>
#include <rapidjson/document.h>

#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

typedef rapidjson::Writer<rapidjson::StringBuffer> RAPIDJSON_WRITER;

class JsonObjBatch
{
public:
	JsonObjBatch(RAPIDJSON_WRITER& writer)
		: m_writer(writer)
	{
		m_writer.StartObject();
	}
	~JsonObjBatch()
	{
		m_writer.EndObject();
	}
private:
	RAPIDJSON_WRITER& m_writer;
};

class JsonArrayBatch
{
public:
	JsonArrayBatch(RAPIDJSON_WRITER& writer)
		: m_writer(writer)
	{
		m_writer.StartArray();
	}
	~JsonArrayBatch()
	{
		m_writer.EndArray();
	}
private:
	RAPIDJSON_WRITER& m_writer;
};

class CurveModel;

namespace JsonHelper
{
	void AddStringList(const QStringList& list, RAPIDJSON_WRITER& writer);
	void AddVariantList(const QVariantList& list, const QString& type, RAPIDJSON_WRITER& writer, bool fillInvalid = true);
	void AddParams(const QString& op,
		const QString& ident,
		const QString& name,
		const QVariant& defl,
		const QString& descType,
		RAPIDJSON_WRITER& writer
		);
	void AddVariant(const QVariant& var, const QString& type, RAPIDJSON_WRITER& writer, bool fillInvalid);
	void AddVariantToStringList(const QVariantList& list, RAPIDJSON_WRITER& writer);
	CurveModel* _parseCurveModel(const rapidjson::Value& jsonCurve, QObject* parentRef);
	void dumpCurveModel(const CurveModel* pModel, RAPIDJSON_WRITER& writer);
}



#endif