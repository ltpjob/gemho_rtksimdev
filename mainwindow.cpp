#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "bsp_thr.h"
#include "datafifo.h"
#include <QString>
#include <QFile>
#include<QDebug>
#include <QDateTime>
#include <unistd.h>
#include <QtNetwork>

typedef struct tagGemhoRtkSimDev{
    DataFifo df;
    BSP_ThrHndl netDevThread;
    QString ip;
    quint16 port;
    int runFlag;
    quint32 cpuid[3];
}GemhoRtkSimDev;

#pragma pack(1)
typedef struct tagupComDataHead
{
    quint8  start[2];
    qint16 type;
    qint32 size;
}upComDataHead;
#pragma pack()


static int readOneSecData(QFile *f, QByteArray *data, QDateTime *dtime)
{
    QByteArray td;
    QString ts;

    data->clear();
    while(1)
    {
        td = f->readLine(2048);
        if(td.size() <= 0)
        {
            data->clear();
            return -1;
        }

        data->append(td);

        ts = td;
        if(ts.contains("$GNZDA,", Qt::CaseInsensitive) == true)
        {
            QStringList slist;

            slist = ts.split(",");
            slist.pop_front();
            slist.pop_back();
            slist.pop_back();

            *dtime = QDateTime::fromString(slist.join(","),
                                  "hhmmss.z,dd,MM,yyyy");
//            qDebug()<<slist.join(",")<<dtime->time().second();
            break;

        }
    }

    return 0;
}

static void *process_fileDataCollect(void *args)
{
    QString name_rover = "E:/GNSS/gushan_backup/20170730-67161949555780670670FF52.txt";
    QString name_base = "E:/GNSS/gushan_backup/20170730-8719512552536752066EFF53.txt";
    QFile file_rover(name_rover);
    QFile file_base(name_base);
    int flag_datatus = 0;
    QList<void *> *listDev = (QList<void *> *)args;

    file_rover.open(QIODevice::ReadOnly);
    file_base.open(QIODevice::ReadOnly);

    while(1)
    {
        QByteArray data_rover;
        QByteArray data_base;
        QDateTime dt_rover;
        QDateTime dt_base;
        QTime time;

        time.start();
        while(1)
        {
            if(flag_datatus == 0 || flag_datatus == 2)
                readOneSecData(&file_rover, &data_rover, &dt_rover);

            if(flag_datatus == 0 || flag_datatus == 1)
                readOneSecData(&file_base, &data_base, &dt_base);

            if(dt_base.secsTo(dt_rover) == 0)
            {

                while(time.elapsed() < 1000)
                {
                    usleep(50*1000);
                }
                time.restart();

                for(QList<void *>::iterator iter = listDev->begin(); iter != listDev->end(); iter++)
                {
                    GemhoRtkSimDev *handle = (GemhoRtkSimDev *)*iter;
                    if(iter == listDev->begin())
                    {
                        handle->df.pushData(data_base);
                    }
                    else
                    {
                        handle->df.pushData(data_rover);
                    }
                }

                qDebug()<<dt_rover<<dt_base;
                flag_datatus = 0;
            }
            else if(dt_base.secsTo(dt_rover) > 0)
            {
                flag_datatus = 1;
            }
            else
            {
                flag_datatus = 2;
            }
        }

    }

    return NULL;
}



static void *process_simDev(void *args)
{
    GemhoRtkSimDev *handle = (GemhoRtkSimDev *)args;
    QTcpServer tcpServ;
    QTime time;

    tcpServ.listen(QHostAddress::Any, handle->port);
    while(handle->runFlag == 1)
    {
        if(tcpServ.waitForNewConnection(100) == false)
            continue;

        QTcpSocket *tcp = tcpServ.nextPendingConnection();
        time.start();

        while(handle->runFlag == 1)
        {
            qint64 len = 0;
            upComDataHead head;
            QByteArray tmpData;

            if(time.elapsed() >= 1000)
            {
                head.start[0] = 0x55;
                head.start[1] = 0xaa;
                head.type = 1;
                head.size = 2;

                len = tcp->write((char *)&head, sizeof(head));
                if(len<0)
                {
                    tcp->close();
                    delete tcp;
                    break;
                }
                time.restart();
            }

            if(handle->df.getDataSize() > 0)
            {
                tmpData.clear();
                tmpData = handle->df.popAllData();
                head.start[0] = 0x55;
                head.start[1] = 0xaa;
                head.type = 1;
                head.size = tmpData.size() + sizeof(handle->cpuid);
                tmpData.prepend((char *)handle->cpuid, sizeof(handle->cpuid));
                tmpData.prepend((char *)&head, sizeof(head));
                len = tcp->write(tmpData);
                if(len<0)
                {
                    tcp->close();
                    delete tcp;
                    break;
                }
                if(len != tmpData.size() && len>0)
                    qDebug()<<"len != tmpData.size()";
            }

            if(tcp->bytesAvailable()>0 || tcp->waitForReadyRead(10))
            {
                tmpData = tcp->readAll();
            }
        }
    }

    return NULL;
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_listdev.clear();
    for(int i=0; i<4; i++)
    {
        GemhoRtkSimDev *handle = new GemhoRtkSimDev;

        handle->cpuid[2] = 0xaaaa;
        handle->cpuid[1] = 0xbbbb;
        handle->cpuid[0] = 0+i;
        handle->port = 5886+i;
        handle->runFlag = 1;

        BSP_thrCreate(&handle->netDevThread, process_simDev, BSP_THR_PRI_DEFAULT, BSP_THR_STACK_SIZE_DEFAULT, handle);
        m_listdev.push_back(handle);
    }

    BSP_thrCreate(&m_fileReadThread, process_fileDataCollect, BSP_THR_PRI_DEFAULT, BSP_THR_STACK_SIZE_DEFAULT, &m_listdev);
}

MainWindow::~MainWindow()
{
    delete ui;
}
