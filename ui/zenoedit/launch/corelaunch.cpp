#include "corelaunch.h"
#include <zenomodel/include/modelrole.h>
#include <zenomodel/include/jsonhelper.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/utils/scope_exit.h>
#include <zeno/extra/GlobalComm.h>
#include <zeno/extra/GlobalStatus.h>
#include <zeno/utils/logger.h>
#include <zeno/core/Graph.h>
#include <zeno/zeno.h>
#include <zeno/types/StringObject.h>
#include "zenoapplication.h"
#include "zenomainwindow.h"
#include "settings/zsettings.h"
#include <zenomodel/include/graphsmanagment.h>
#include "serialize.h"
#if !defined(ZENO_MULTIPROCESS) || !defined(ZENO_IPC_USE_TCP)
#include <thread>
#include <mutex>
#include <atomic>
#endif
#ifdef ZENO_MULTIPROCESS
#include <QProcess>
#include "viewdecode.h"
#ifdef ZENO_IPC_USE_TCP
#include "ztcpserver.h"
#endif
#endif

//#define DEBUG_SERIALIZE

namespace {

#if !defined(ZENO_MULTIPROCESS) || !defined(ZENO_IPC_USE_TCP)
struct ProgramRunData {
    enum ProgramState {
        kStopped = 0,
        kRunning,
        kQuiting,
    };

    inline static std::mutex g_mtx;
    inline static std::atomic<ProgramState> g_state{kStopped};

    std::string progJson;

#ifdef ZENO_MULTIPROCESS
    inline static std::unique_ptr<QProcess> g_proc;
#endif

    void operator()() const {
        std::unique_lock lck(g_mtx);
        start();
#ifdef ZENO_MULTIPROCESS
        if (g_proc) {
            zeno::log_warn("terminating runner process");
            g_proc->terminate();
            g_proc->waitForFinished(-1);
            int code = g_proc->exitCode();
            g_proc = nullptr;
            zeno::log_info("runner process terminated with {}", code);
        }
#endif
        g_state = kStopped;
    }

    void reportStatus(zeno::GlobalStatus const &stat) const {
        if (!stat.failed()) return;
        zeno::log_error("error in {}, message {}", stat.nodeName, stat.error->message);
        auto nodeName = stat.nodeName.substr(0, stat.nodeName.find(':'));
        zenoApp->graphsManagment()->appendErr(QString::fromStdString(nodeName),
                                              QString::fromStdString(stat.error->message));
    }

    bool chkfail() const {
        auto globalStatus = zeno::getSession().globalStatus.get();
        if (globalStatus->failed()) {
            reportStatus(*globalStatus);
            return true;
        }
        return false;
    }

    void start() const {
        zeno::log_debug("launching program...");
        zeno::log_debug("program JSON: {}", progJson);

#ifndef ZENO_MULTIPROCESS
        auto session = &zeno::getSession();
        session->globalComm->clearState();
        session->globalState->clearState();
        session->globalStatus->clearState();

        int cacheNum = 0;
        bool bZenCache = initZenCache(nullptr, cacheNum);

        auto graph = session->createGraph();
        graph->loadGraph(progJson.c_str());

        //QSettings settings("ZenusTech", "Zeno");
        //QVariant nas_loc_v = settings.value("nas_loc");
        //if (!nas_loc_v.isNull()) {
            //QString nas_loc = nas_loc_v.toString();

            //for (auto &[k, n]: graph->nodes) {
                //for (auto & [sn, sv]: n->inputs) {
                    //auto p = std::dynamic_pointer_cast<zeno::StringObject>(sv);
                    //if (p) {
                        //std::string &str = p->get();
                        //if (str.find("$NASLOC") == 0) {
                            //str = str.replace(0, 7, nas_loc.toStdString());
                        //}
                    //}
                //}
            //}
        //}

        if (chkfail()) return;
        if (g_state == kQuiting) return;

        session->globalComm->initFrameRange(graph->beginFrameNumber, graph->endFrameNumber);
        for (int frame = graph->beginFrameNumber; frame <= graph->endFrameNumber; frame++) {
            zeno::log_debug("begin frame {}", frame);
            session->globalState->frameid = frame;
            session->globalComm->newFrame();
            //corresponding to processPacket in viewdecode.cpp
            if (zenoApp->getMainWindow())
                zenoApp->getMainWindow()->updateViewport(QString::fromStdString("newFrame"));
            session->globalState->frameBegin();
            while (session->globalState->substepBegin())
            {
                if (g_state == kQuiting)
                    return;
                graph->applyNodesToExec();
                session->globalState->substepEnd();
                if (chkfail()) return;
            }
            if (g_state == kQuiting) return;
            session->globalState->frameEnd();
            if (bZenCache)
                session->globalComm->dumpFrameCache(frame);
            session->globalComm->finishFrame();
            if (zenoApp->getMainWindow())
                zenoApp->getMainWindow()->updateViewport(QString::fromStdString("finishFrame"));
            zeno::log_debug("end frame {}", frame);
            if (chkfail()) return;
        }
        if (session->globalStatus->failed()) {
            reportStatus(*session->globalStatus);
        }
        zeno::log_debug("program finished");
#else
        //auto execDir = QCoreApplication::applicationDirPath().toStdString();
//#if defined(Q_OS_WIN)
        //auto runnerCmd = execDir + "\\zenorunner.exe";
//#else
        //auto runnerCmd = execDir + "/zenorunner";
//#endif

        g_proc = std::make_unique<QProcess>();
        g_proc->setInputChannelMode(QProcess::InputChannelMode::ManagedInputChannel);
        g_proc->setReadChannel(QProcess::ProcessChannel::StandardOutput);
        g_proc->setProcessChannelMode(QProcess::ProcessChannelMode::ForwardedErrorChannel);
        int sessionid = zeno::getSession().globalState->sessionid;
        g_proc->start(QCoreApplication::applicationFilePath(), QStringList({"-runner", QString::number(sessionid)}));
        if (!g_proc->waitForStarted(-1)) {
            zeno::log_warn("process failed to get started, giving up");
            return;
        }

        g_proc->write(progJson.data(), progJson.size());
        g_proc->closeWriteChannel();

        std::vector<char> buf(1<<20); // 1MB
        viewDecodeClear();
        zeno::scope_exit decodeFin{[] {
            viewDecodeFinish();
        }};

        while (g_proc->waitForReadyRead(-1)) {
            while (!g_proc->atEnd()) {
                if (g_state == kQuiting) return;
                qint64 redSize = g_proc->read(buf.data(), buf.size());
                zeno::log_debug("g_proc->read got {} bytes (ping test has 19)", redSize);
                if (redSize > 0) {
                    viewDecodeAppend(buf.data(), redSize);
                }
            }
            if (chkfail()) break;
            if (g_state == kQuiting) return;
        }
        zeno::log_debug("still not ready-read, assume exited");
        decodeFin.reset();

        buf.clear();
        g_proc->terminate();
        int code = g_proc->exitCode();
        g_proc = nullptr;
        zeno::log_info("runner process exited with {}", code);
#endif
    }
};
#endif

void launchProgramJSON(std::string progJson, LAUNCH_PARAM param)
{
#if defined(ZENO_MULTIPROCESS) && defined(ZENO_IPC_USE_TCP)
    ZTcpServer *pServer = zenoApp->getServer();
    if (pServer)
    {
        pServer->startProc(std::move(progJson), param);
    }
#else
    std::unique_lock lck(ProgramRunData::g_mtx, std::try_to_lock);
    if (!lck.owns_lock()) {
        zeno::log_debug("background process already running, give up");
        return;
    }

    ProgramRunData::g_state = ProgramRunData::kRunning;
    std::thread thr{ProgramRunData{std::move(progJson)}};
    thr.detach();
#endif
}


void killProgramJSON()
{
    zeno::log_info("killing current program");
#if defined(ZENO_MULTIPROCESS) && defined(ZENO_IPC_USE_TCP)
    ZTcpServer *pServer = zenoApp->getServer();
    if (pServer)
    {
        pServer->killProc();
    }
#else
    ProgramRunData::g_state = ProgramRunData::kQuiting;
#endif
}

}

void launchProgram(IGraphsModel* pModel, LAUNCH_PARAM param)
{
	rapidjson::StringBuffer s;
	RAPIDJSON_WRITER writer(s);
    {
        JsonArrayBatch batch(writer);
        JsonHelper::AddVariantList({"setBeginFrameNumber", param.beginFrame}, "int", writer);
        JsonHelper::AddVariantList({"setEndFrameNumber", param.endFrame}, "int", writer);
        serializeScene(pModel, writer, param.applyLightAndCameraOnly, param.applyMaterialOnly);
    }
    std::string progJson(s.GetString());
#ifdef DEBUG_SERIALIZE
    QString qstrJson = QString::fromStdString(progJson);
    QFile f("serialize.json");
    f.open(QIODevice::WriteOnly);
    f.write(qstrJson.toUtf8());
    f.close();
#endif
    launchProgramJSON(std::move(progJson), param);
    pModel->clearNodeDataChanged();
}

void killProgram() {
    killProgramJSON();
}

bool initZenCache(char* pCachePath, int& cacheNum)
{
    QSettings settings(zsCompanyName, zsEditor);
    const QString& cachenum = settings.value("zencachenum").toString();
    bool bEnableCache = settings.value("zencache-enable").toBool();
    cacheNum = cachenum.toInt();

    QString qsPath = QString::fromLocal8Bit(pCachePath);
    bEnableCache = bEnableCache && QFileInfo(qsPath).isDir() && cacheNum > 0;
    if (bEnableCache) {
        zeno::getSession().globalComm->frameCache(qsPath.toStdString(), cacheNum);
    }
    else {
        cacheNum = 0;
        zeno::getSession().globalComm->frameCache("", 0);
    }
    return bEnableCache;
}
