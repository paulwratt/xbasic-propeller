

#include "mainwindow.h"
#include "qextserialenumerator.h"
#include "PropellerID.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    /* setup application registry info */
    QCoreApplication::setOrganizationName(publisherKey);
    QCoreApplication::setOrganizationDomain(publisherComKey);
    QCoreApplication::setApplicationName(xBasicGuiKey);

    /* global settings */
    settings = new QSettings(publisherKey, xBasicGuiKey, this);

    /* setup properties dialog */
    propDialog = new Properties(this);
    connect(propDialog,SIGNAL(accepted()),this,SLOT(propertiesAccepted()));

    /* new xBasicConfig class */
    xBasicConfig = new XBasicConfig();

    projectModel = NULL;
    referenceModel = NULL;

    /* setup gui components */
    setupFileMenu();
    setupHelpMenu();
    setupToolBars();

    /* main container */
    setWindowTitle(tr("xBasic IDE"));
    QSplitter *vsplit = new QSplitter(this);
    setCentralWidget(vsplit);

    /* minimum window height */
    this->setMinimumHeight(500);

    /* project tools */
    setupProjectTools(vsplit);

    /* start with an empty file if fresh install */
    newFile();

    /* status bar for progressbar */
    QStatusBar *statusBar = new QStatusBar();

    sizeLabel = new QLabel;
    msgLabel  = new QLabel;

    progress = new QProgressBar();
    //progress->setMaximumSize(100,25);
    progress->setValue(0);
    progress->setMinimum(0);
    progress->setMaximum(100);
    progress->setTextVisible(false);
    progress->setVisible(false);

    statusBar->addPermanentWidget(msgLabel,70);
    statusBar->addPermanentWidget(sizeLabel,15);
    statusBar->addPermanentWidget(progress,15);

    this->setStatusBar(statusBar);


    /* get app settings at startup and before any compiler call */
    getApplicationSettings();

    initBoardTypes();

    /* setup the port listener */
    portListener = new PortListener();

    /* get available ports at startup */
    enumeratePorts();

#if 0
#if defined(Q_WS_WIN32)
    /* connect windows USB change detect signal to enumerator */
    connect(this,SIGNAL(doPortEnumerate()),this, SLOT(enumeratePortsEvent()));
#endif
#else
    portConnectionMonitor = new PortConnectionMonitor();
    connect(portConnectionMonitor, SIGNAL(portChanged()), this, SLOT(enumeratePortsEvent()));
#endif

    /* these are read once per app startup */
    QVariant lastportv  = settings->value(lastPortNameKey);
    if(lastportv.canConvert(QVariant::String))
        portName = lastportv.toString();

    /* setup the first port displayed in the combo box */
    if(cbPort->count() > 0) {
        int ndx = 0;
        if(portName.length() != 0) {
            for(int n = cbPort->count()-1; n > -1; n--)
                if(cbPort->itemText(n) == portName)
                {
                    ndx = n;
                    break;
                }
        }
        setCurrentPort(ndx);
    }

    /* setup the terminal dialog box */
    term = new Terminal(this);
    connect(term,SIGNAL(accepted()),this,SLOT(terminalClosed()));
    connect(term,SIGNAL(rejected()),this,SLOT(terminalClosed()));

    /* tell port listener to use terminal editor for i/o */
    termEditor = term->getEditor();
    portListener->setTerminalWindow(termEditor);
    term->setPortListener(portListener);

    /* load the last file into the editor to make user happy */
    QVariant lastfilev = settings->value(lastFileNameKey);
    QString fileName;
    if(!lastfilev.isNull()) {
        if(lastfilev.canConvert(QVariant::String)) {
            fileName = lastfilev.toString();
            openFileName(fileName);
            setProject(); // last file is always first project
        }
    }

    /* load the last directory to make user happy */
    lastDirectory = fileName.mid(0,fileName.lastIndexOf("/")+1);
    QVariant lastdirv = settings->value(lastDirectoryKey, lastDirectory);
    if(!lastdirv.isNull()) {
        if(lastdirv.canConvert(QVariant::String)) {
            lastDirectory = lastdirv.toString();
        }
    }

    hardwareDialog = new Hardware(this);
    connect(hardwareDialog,SIGNAL(accepted()),this,SLOT(initBoardTypes()));
}

#if defined(Q_WS_WIN32)
bool MainWindow::winEvent(MSG *message, long *result)
{
    if (message->message==WM_DEVICECHANGE)
    {
        qDebug() << ("WM_DEVICECHANGE message received");
        if (message->wParam==DBT_DEVICEARRIVAL) {
            qDebug() << ("A new device has arrived");
            emit doPortEnumerate();
        }
        if (message->wParam==DBT_DEVICEREMOVECOMPLETE) {
            qDebug() << ("A device has been removed");
            emit doPortEnumerate();
        }
    }
    return false;
}
#endif

void MainWindow::keyHandler(QKeyEvent* event)
{
    //qDebug() << "MainWindow::keyHandler";
    int key = event->key();
    switch(key)
    {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        key = '\n';
        break;
    case Qt::Key_Backspace:
        key = '\b';
        break;
    default:
        if(key & Qt::Key_Escape)
            return;
        QChar c = event->text().at(0);
        key = (int)c.toAscii();
        break;
    }
    QByteArray barry;
    barry.append((char)key);
    portListener->send(barry);
}

void MainWindow::terminalEditorTextChanged()
{
    QString text = termEditor->toPlainText();
}

/*
 * get the application settings from the registry for compile/startup
 */
void MainWindow::getApplicationSettings()
{
    QFile file;
    QVariant compv = settings->value(compilerKey);

    if(compv.canConvert(QVariant::String))
        xBasicCompiler = compv.toString();

    if(!file.exists(xBasicCompiler))
    {
        propDialog->showProperties();
    }

    /* get the separator used at startup */
    QString appPath = QCoreApplication::applicationDirPath ();
    if(appPath.indexOf('\\') > -1)
        xBasicSeparator = "\\";
    else
        xBasicSeparator = "/";

    /* get the compiler path */
    if(xBasicCompiler.indexOf('\\') > -1) {
        xBasicCompilerPath = xBasicCompiler.mid(0,xBasicCompiler.lastIndexOf('\\')+1);
    }
    else if(xBasicCompiler.indexOf('/') > -1) {
        xBasicCompilerPath = xBasicCompiler.mid(0,xBasicCompiler.lastIndexOf('/')+1);
    }

    /* get the include path and config file set by user */
    QVariant incv = settings->value(includesKey);
    QVariant cfgv = settings->value(configFileKey);

    /* convert registry values to strings */
    if(incv.canConvert(QVariant::String))
        xBasicIncludes = incv.toString();

    if(cfgv.canConvert(QVariant::String))
        xBasicCfgFile = cfgv.toString();

    if(!file.exists(xBasicCfgFile))
    {
        propDialog->showProperties();
    }
    else
    {
        /* load boards in case there were changes */
        xBasicConfig->loadBoards(xBasicCfgFile);
    }
}

void MainWindow::exitSave()
{
    bool saveAll = false;
    QMessageBox mbox(QMessageBox::Question, "Save File?", "",
                     QMessageBox::Discard | QMessageBox::Save | QMessageBox::SaveAll, this);

    for(int tab = editorTabs->count()-1; tab > -1; tab--)
    {
        QString tabName = editorTabs->tabText(tab);
        if(tabName.at(tabName.length()-1) == '*')
        {
            mbox.setInformativeText(tr("Save File: ") + tabName.mid(0,tabName.indexOf(" *")) + tr(" ?"));
            if(saveAll)
            {
                saveFileByTabIndex(tab);
            }
            else
            {
                int ret = mbox.exec();
                switch (ret) {
                    case QMessageBox::Discard:
                        // Don't Save was clicked
                        return;
                        break;
                    case QMessageBox::Save:
                        // Save was clicked
                        saveFileByTabIndex(tab);
                        break;
                    case QMessageBox::SaveAll:
                        // save all was clicked
                        saveAll = true;
                        break;
                    default:
                        // should never be reached
                        break;
                }
            }
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    portListener->close();

    exitSave(); // find

    portConnectionMonitor->stop();

    QString filestr = editorTabs->tabToolTip(editorTabs->currentIndex());
    QString boardstr = cbBoard->itemText(cbBoard->currentIndex());
    QString portstr = cbPort->itemText(cbPort->currentIndex());

    settings->setValue(lastFileNameKey,filestr);
    settings->setValue(lastBoardNameKey,boardstr);
    settings->setValue(lastPortNameKey,portstr);
    settings->setValue(lastDirectoryKey, lastDirectory);
    QApplication::quit();
}

void MainWindow::newFile()
{
    fileChangeDisable = true;
    setupEditor();
    int tab = editors->count()-1;
    editorTabs->addTab(editors->at(tab),(const QString&)untitledstr);
    editorTabs->setCurrentIndex(tab);
    fileChangeDisable = false;
}

void MainWindow::openFile(const QString &path)
{
    QString fileName = path;

    if (fileName.isNull())
        fileName = QFileDialog::getOpenFileName(this,
            tr("Open File"), lastDirectory, "xBasic Files (*.bas)");
    openFileName(fileName);
    lastDirectory = fileName.mid(0,fileName.lastIndexOf("/")+1);
    setProject();
    /*
    if(projectFile.length() == 0) {
        setProject();
    }
    else if(editorTabs->count() == 1) {
        setProject();
    }
    */
}

void MainWindow::openFileName(QString fileName)
{
    QString data;
    if (!fileName.isEmpty()) {
        QFile file(fileName);
//        if (file.open(QFile::ReadOnly | QFile::Text))
        if (file.open(QFile::ReadOnly))
        {
            data = file.readAll();
            QString sname = this->shortFileName(fileName);
            if(editorTabs->count()>0) {
                for(int n = editorTabs->count()-1; n > -1; n--) {
                    if(editorTabs->tabText(n) == sname) {
                        setEditorTab(n, sname, fileName, data);
                        file.close();
                        setProject();
                        return;
                    }
                }
            }
            if(editorTabs->tabText(0) == untitledstr) {
                setEditorTab(0, sname, fileName, data);
                file.close();
                setProject();
                return;
            }
            newFile();
            setEditorTab(editorTabs->count()-1, sname, fileName, data);
            file.close();
            setProject();
        }
    }
}


void MainWindow::saveFile(const QString &path)
{
    try {
        int n = this->editorTabs->currentIndex();
        QString fileName = editorTabs->tabToolTip(n);
        QString data = editors->at(n)->toPlainText();

        this->editorTabs->setTabText(n,shortFileName(fileName));
        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(data.toAscii());
                file.close();
                setProject();
            }
        }
    } catch(...) {
    }
}

void MainWindow::saveFileByTabIndex(int tab)
{
    try {
        QString fileName = editorTabs->tabToolTip(tab);
        QString data = editors->at(tab)->toPlainText();

        this->editorTabs->setTabText(tab,shortFileName(fileName));
        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(data.toAscii());
                file.close();
            }
        }
    } catch(...) {
    }
}

void MainWindow::saveAsFile(const QString &path)
{
    try {
        int n = this->editorTabs->currentIndex();
        QString data = editors->at(n)->toPlainText();
        QString fileName = path;

        if (fileName.isEmpty())
            fileName = QFileDialog::getSaveFileName(this,
                tr("Save As File"), lastDirectory, "xBasic Files (*.bas)");

        if (fileName.isEmpty())
            return;
        lastDirectory = fileName.mid(0,fileName.lastIndexOf("/")+1);

        this->editorTabs->setTabText(n,shortFileName(fileName));
        editorTabs->setTabToolTip(n,fileName);

        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(data.toAscii());
                file.close();
            }
            setProject();
        }
    } catch(...) {
    }
}

void MainWindow::fileChanged()
{
    if(fileChangeDisable)
        return;
    int index = editorTabs->currentIndex();
    QString name = editorTabs->tabText(index);
    QString fileName = editorTabs->tabToolTip(index);
    if(!QFile::exists(fileName))
        return;
    QFile file(fileName);
    if(file.open(QFile::ReadOnly))
    {
        QString cur = editors->at(index)->toPlainText();
        QString text = file.readAll();
        file.close();
        if(text == cur) {
            if(name.at(name.length()-1) == '*')
                editorTabs->setTabText(index, this->shortFileName(fileName));
            return;
        }
    }
    if(name.at(name.length()-1) != '*') {
        name += tr(" *");
        editorTabs->setTabText(index, name);
    }
}

void MainWindow::printFile(const QString &path)
{
    /*
    QString fileName = path;
    QString data = editor->toPlainText();

    if (!fileName.isEmpty()) {
    }
    */
}

void MainWindow::zipFile(const QString &path)
{
    /*
    QString fileName = path;
    QString data = editor->toPlainText();

    if (!fileName.isEmpty()) {
    }
    */
}

void MainWindow::setProject()
{
    int index = editorTabs->currentIndex();
    QString fileName = editorTabs->tabToolTip(index);
    QString text = editors->at(index)->toPlainText();

    setWindowTitle(tr("xBasic IDE")+" "+fileName);

    if(fileName.length() != 0)
    {
        updateProjectTree(fileName,text);
        updateReferenceTree(fileName,text);
    }
    else {
        delete projectModel;
        projectModel = NULL;
        delete referenceModel;
        referenceModel = NULL;
    }
}

void MainWindow::hardware()
{
    hardwareDialog->loadBoards();
    hardwareDialog->show();
}

void MainWindow::properties()
{
    propDialog->showProperties();
}
void MainWindow::propertiesAccepted()
{
    getApplicationSettings();
    initBoardTypes();
}

void MainWindow::setCurrentBoard(int index)
{
    boardName = cbBoard->itemText(index);
    cbBoard->setCurrentIndex(index);
}

void MainWindow::setCurrentPort(int index)
{
    portName = cbPort->itemText(index);
    cbPort->setCurrentIndex(index);
    if(portName.length()) {
        portListener->init(portName, BAUD115200);  // signals get hooked up internally
    }
}

void MainWindow::checkAndSaveFiles()
{
    if(projectModel == NULL)
        return;
    QString title = projectModel->getTreeName();
    QString modTitle = title + " *";
    for(int tab = editorTabs->count()-1; tab > -1; tab--)
    {
        QString tabName = editorTabs->tabText(tab);
        if(tabName == modTitle)
        {
            saveFileByTabIndex(tab);
            editorTabs->setTabText(tab,title);
        }
    }

    int len = projectModel->rowCount();
    for(int n = 0; n < len; n++)
    {
        QModelIndex root = projectModel->index(n,0);
        QVariant vs = projectModel->data(root, Qt::DisplayRole);
        if(!vs.canConvert(QVariant::String))
            continue;
        QString name = vs.toString();
        QString modName = name + " *";
        for(int tab = editorTabs->count()-1; tab > -1; tab--)
        {
            QString tabName = editorTabs->tabText(tab);
            if(tabName == modName)
            {
                saveFileByTabIndex(tab);
                editorTabs->setTabText(tab,name);
            }
        }
    }
}

int  MainWindow::checkCompilerInfo()
{
    QMessageBox mbox(QMessageBox::Critical,tr("Build Error"),"",QMessageBox::Ok);
    if(xBasicCompiler.length() == 0) {
        mbox.setInformativeText(tr("Please specify compiler application in properties."));
        mbox.exec();
        return -1;
    }
    if(xBasicIncludes.length() == 0) {
        mbox.setInformativeText(tr("Please specify include path in properties."));
        mbox.exec();
        return -1;
    }
    return 0;
}

QStringList MainWindow::getCompilerParameters(QString copts)
{
    // use the projectFile instead of the current tab file
    QString srcpath = projectFile;
    //QString srcpath = this->editorTabs->tabToolTip(this->editorTabs->currentIndex());
    srcpath = QDir::fromNativeSeparators(srcpath);
    srcpath = srcpath.mid(0,srcpath.lastIndexOf(xBasicSeparator)+1);

    portName = cbPort->itemText(cbPort->currentIndex());    // TODO should be itemToolTip
    boardName = cbBoard->itemText(cbBoard->currentIndex());

    QStringList args;
    args.append(("-d")); // tell compiler to insert a delay for terminal startup
    args.append(("-b"));
    args.append(boardName);
    args.append(("-p"));
    args.append(portName);
    args.append(("-I"));
    args.append(xBasicIncludes);
    args.append(("-I"));
    args.append(srcpath);
    args.append(projectFile);
    args.append(copts);

    qDebug() << args;
    return args;
}

int  MainWindow::runCompiler(QString copts)
{
    if(projectModel == NULL || projectFile.isNull()) {
        QMessageBox mbox(QMessageBox::Critical, "Error No Project",
            "Please select a tab and press F4 to set main project file.", QMessageBox::Ok);
        mbox.exec();
        return -1;
    }

    // don't allow if no port available
    if(copts.length() > 0 && cbPort->count() < 1) {
        QMessageBox mbox(QMessageBox::Critical, tr("No Serial Port"),
             tr("Serial port not available.")+" "+tr("Connect a USB Propeller board, turn it on, and try again."),
             QMessageBox::Ok);
        mbox.exec();
        return -1;
    }

#ifdef AUTOPORT
    if(copts.compare("-v")) {
        checkConfigSerialPort();
    }
#endif
    setCurrentPort(cbPort->currentIndex());

    QString result;
    int exitCode = 0;
    int exitStatus = 0;

    int index = editorTabs->currentIndex();
    QString fileName = editorTabs->tabToolTip(index);
    QString text = editors->at(index)->toPlainText();

    updateProjectTree(fileName,text);
    updateReferenceTree(fileName,text);

    sizeLabel->setText("");
    msgLabel->setText("");

    progress->setValue(0);
    progress->setVisible(true);

    getApplicationSettings();
    if(checkCompilerInfo()) {
        return -1;
    }

    QStringList args = getCompilerParameters(copts);

    btnConnected->setChecked(false);
    connectButton();            // disconnect uart before use

    checkAndSaveFiles();

    QMessageBox mbox;
    mbox.setStandardButtons(QMessageBox::Ok);

    QProcess proc(this);
    this->proc = &proc;

    connect(this->proc, SIGNAL(readyReadStandardOutput()),this,SLOT(procReadyRead()));
    proc.setProcessChannelMode(QProcess::MergedChannels);

    proc.setWorkingDirectory(xBasicCompilerPath);

    proc.start(xBasicCompiler,args);

    if(!proc.waitForStarted()) {
        mbox.setInformativeText(tr("Could not start compiler."));
        mbox.exec();
        goto error;
    }

    progress->setValue(10);

    if(!proc.waitForFinished()) {
        mbox.setInformativeText(tr("Error waiting for compiler to finish."));
        mbox.exec();
        goto error;
    }

    progress->setValue(20);

    exitCode = proc.exitCode();
    exitStatus = proc.exitStatus();

    mbox.setInformativeText(compileResult);
    msgLabel->setText("");

    if(exitStatus == QProcess::CrashExit)
    {
        mbox.setText(tr("xBasic Compiler Crashed"));
        mbox.exec();
        goto error;
    }
    progress->setValue(30);
    if(exitCode != 0)
    {
        if(compileResult.toLower().indexOf("helper") > 0) {
            mbox.setInformativeText(result +
                 "\nDid you set the right board type?" +
                 "\nHUB and C3 set 80MHz clock." +
                 "\nHUB96 and SSF set 96MHz clock.");
        }
        mbox.setText(tr("xBasic Compile Error"));
        mbox.exec();
        goto error;
    }
    progress->setValue(40);
    if(compileResult.indexOf("error") > -1)
    { // just in case we get an error without exitCode
        mbox.setText(tr("xBasic Compile Error"));
        mbox.exec();
        goto error;
    }

    msgLabel->setText("Build Complete");
    progress->setValue(100);
    progress->setVisible(false);
    disconnect(this->proc, SIGNAL(readyReadStandardOutput()),this,SLOT(procReadyRead()));
    return 0;

    error:

    sizeLabel->setText("Error");

    exitCode = proc.exitCode();
    exitStatus = proc.exitStatus();

    progress->setValue(100);
    progress->setVisible(false);
    disconnect(this->proc, SIGNAL(readyReadStandardOutput()),this,SLOT(procReadyRead()));
    return -1;
}

void MainWindow::compilerError(QProcess::ProcessError error)
{
    qDebug() << error;
}

void MainWindow::compilerFinished(int exitCode, QProcess::ExitStatus status)
{
    qDebug() << exitCode << status;
}

void MainWindow::procReadyRead()
{
    QByteArray bytes = this->proc->readAllStandardOutput();
    if(bytes.length() == 0)
        return;

#if defined(Q_WS_WIN32)
    QString eol("\r");
#else
    QString eol("\n");
#endif
    bytes = bytes.replace("\r\n","\n");

    compileResult = QString(bytes);
    QStringList lines = QString(bytes).split("\n",QString::SkipEmptyParts);
    if(bytes.contains("bytes")) {
        for (int n = 0; n < lines.length(); n++) {
            QString line = lines[n];
            if(line.length() > 0) {
                if(line.indexOf("\r") > -1) {
                    QStringList more = line.split("\r",QString::SkipEmptyParts);
                    lines.removeAt(n);
                    for(int m = more.length()-1; m > -1; m--) {
                        QString ms = more.at(m);
                        if(ms.contains("bytes",Qt::CaseInsensitive))
                            lines.insert(n,more.at(m));
                        if(ms.contains("loading",Qt::CaseInsensitive))
                            lines.insert(n,more.at(m));
                    }
                }
            }
        }
    }
    int vmsize = 1916;

    bool isCompileSz = bytes.contains(" entry") && bytes.contains(" base") &&
                       bytes.contains(" file offset") && bytes.contains(" size");
    if(isCompileSz) {
        for (int n = 0; n < lines.length(); n++) {
            QString line = lines[n];
            if(line.endsWith(" size")) {
                QStringList sl = line.split(" ", QString::SkipEmptyParts);
                if(sl.count() > 1) {
                    bool ok;
                    int num = QString(sl[0]).toInt(&ok, 16);
                    sizeLabel->setText(QString::number(vmsize+num)+tr(" Total Bytes"));
                    return;
                }
            }
        }
    }

    for (int n = 0; n < lines.length(); n++) {
        QString line = lines[n];
        if(line.length() > 0) {
            /*
            bool iserror = compileResult.contains("error:",Qt::CaseInsensitive);
            if(iserror && line.contains(" line ", Qt::CaseInsensitive)) {
                QStringList sl = line.split(" ", QString::SkipEmptyParts);
                if(sl.count() > 1) {
                    int num = QString(sl[sl.count()-1]).toInt();
                    QPlainTextEdit *ed = editors->at(editorTabs->currentIndex());
                    QTextCursor cur = ed->textCursor();
                    cur.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
                    cur.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, num);
                    cur.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
                    cur.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
                    QApplication::processEvents();
                }
            }
            */
            if(line.contains("Propeller Version",Qt::CaseInsensitive)) {
                msgLabel->setText(line+eol);
                progress->setValue(0);
            }
            else
            if(line.contains("loading image",Qt::CaseInsensitive)) {
                progMax = 0;
                progress->setValue(0);
                msgLabel->setText(line+eol);
            }
            else
            if(line.contains("writing",Qt::CaseInsensitive)) {
                progMax = 0;
                progress->setValue(0);
            }
            else
            if(line.contains("Download OK",Qt::CaseInsensitive)) {
                progress->setValue(100);
                msgLabel->setText(line+eol);
            }
            else
            if(line.contains("sent",Qt::CaseInsensitive)) {
                msgLabel->setText(line+eol);
            }
            else
            if(line.contains("remaining",Qt::CaseInsensitive)) {
                if(progMax == 0) {
                    QString bs = line.mid(0,line.indexOf(" "));
                    progMax = bs.toInt();
                    // include VM size
                    sizeLabel->setText(QString::number(vmsize+progMax)+tr(" Total Bytes"));
                    progMax /= 1024;
                    progMax++;
                    progCount = 0;
                    if(progMax == 0) {
                        progress->setValue(100);
                    }
                }
                if(progMax != 0) {
                    progCount++;
                    progress->setValue(100*progCount/progMax);
                }
                msgLabel->setText(line);
            }
        }
    }

}


void MainWindow::programBuild()
{
    // must have something in the compiler options parameter
    runCompiler("-v");
}

void MainWindow::programBurnEE()
{
    runCompiler("-e");
}

void MainWindow::programRun()
{
    runCompiler("-r");
}

void MainWindow::programDebug()
{
    if(runCompiler("-r"))
        return; // don't start terminal if compile failed

    /*
     * setting the position of a new dialog doesn't work very nice
     * Term dialog will not close/reopen on debug so it doesn't matter.
     */
    term->getEditor()->clear();
    term->show();   // show if hidden

    btnConnected->setChecked(true);
    connectButton();
}

void MainWindow::terminalClosed()
{
    btnConnected->setChecked(false);
    connectButton();
}

void MainWindow::setupHelpMenu()
{
    QMenu *helpMenu = new QMenu(tr("&Help"), this);
    menuBar()->addMenu(helpMenu);

    helpMenu->addAction(QIcon(":/images/helpsymbol.png"), tr("&About"), this, SLOT(about()));
}

void MainWindow::about()
{
    QString version = QString("xBasic IDE Version %1.%2.%3")
            .arg(IDEVERSION).arg(MINVERSION).arg(FIXVERSION);
    QMessageBox::about(this, tr("About xBasic"), version + \
                tr("<p><b>xBasic</b> runs BASIC programs from Propeller HUB<br/>" \
                   "or from user defined external memory.</p>") +
                tr("Visit <a href=\"www.MicroCSource.com/xbasic/help.htm\">MicroCSource.com</a> for more xBasic help."));
}


void MainWindow::projectTreeClicked(QModelIndex index)
{
    if(projectModel == NULL)
        return;
    QVariant vs = projectModel->data(index, Qt::DisplayRole);
    if(vs.canConvert(QVariant::String))
    {
        QString fileName = vs.toString();
        QString userFile = editorTabs->tabToolTip(editorTabs->currentIndex());
        QString userPath = userFile.mid(0,userFile.lastIndexOf(xBasicSeparator)+1);
        QFile file;
        if(file.exists(xBasicIncludes+fileName))
            openFileName(xBasicIncludes+fileName);
        else if(file.exists(userPath+fileName))
            openFileName(userPath+fileName);
    }
}

void MainWindow::referenceTreeClicked(QModelIndex index)
{
    QVariant vs = referenceModel->data(index, Qt::DisplayRole);
    if(vs.canConvert(QVariant::String))
    {
        QString method = vs.toString();
        QString myFile = referenceModel->file(index);
        QString text = 0;

        QFile file;
        if(file.exists(myFile))
        {
            QFile readfile(myFile);
            if (readfile.open(QFile::ReadOnly | QFile::Text))
            {
                text = readfile.readAll();
            }
        }

        if(text != 0)
        {
            QStringList lines = text.split("\n");
            method = method.trimmed();
            int position = 0;
            for(int n = 0; n < lines.length(); n++)
            {
                QString line = lines.at(n);
                position += line.length();
                if(line.indexOf(method) > -1)
                {
                    /* open file in tab if not there already */
                    openFileName(myFile);

                    QPlainTextEdit *editor = editors->at(editorTabs->currentIndex());
                    if(editor != NULL)
                    {
                        /* highlight method - yes, it will be ther */
                        editor->find(method);
                        QTextCursor cur = editor->textCursor();
                        cur.movePosition(QTextCursor::Start);
                        cur.movePosition(QTextCursor::Down,QTextCursor::KeepAnchor,n);

                        /* this part is a little troublesome */
                        QScrollBar *sb = editor->verticalScrollBar();
                        int sbval = sb->value();
                        int sbpage = sb->pageStep();
                        if(sbval > (sbpage*9)/10)
                        {
                            int step = (sb->pageStep()*4)/8;
                            sb->setValue(sb->value()+step);
                        }

                    }
                    return;
                }
            }
        }
    }
}

void MainWindow::closeTab(int index)
{
    fileChangeDisable = true;
    editors->at(index)->setPlainText("");
    editors->remove(index);
    if(editorTabs->count() == 1)
        newFile();
    editorTabs->removeTab(index);
    fileChangeDisable = false;
}

void MainWindow::changeTab(int index)
{
    setProject();
    /*
    if(editorTabs->count() == 1)
        setProject();
    QString fileName = editorTabs->tabToolTip(index);
    QString text = editors->at(index)->toPlainText();

    if(fileName.length() != 0 && text.length() != 0)
    {
        updateProjectTree(fileName,text);
        updateReferenceTree(fileName,text);
    }
    */
}

void MainWindow::addToolButton(QToolBar *bar, QToolButton *btn, QString imgfile)
{
    const QSize buttonSize(24, 24);
    btn->setIcon(QIcon(QPixmap(imgfile.toAscii())));
    btn->setMinimumSize(buttonSize);
    btn->setMaximumSize(buttonSize);
    btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bar->addWidget(btn);
}

void MainWindow::updateProjectTree(QString fileName, QString text)
{
    projectFile = fileName;
    QString s = this->shortFileName(fileName);
    basicPath = fileName.mid(0,fileName.lastIndexOf('/')+1);

    if(projectModel != NULL) delete projectModel;
    projectModel = new TreeModel(s, this);
    if(text.length() > 0) {
        projectModel->xBasicIncludes(fileName, this->xBasicIncludes, this->xBasicSeparator, text, true);
    }
    projectTree->setWindowTitle(s);
    projectTree->setModel(projectModel);
    projectTree->show();

}

void MainWindow::updateReferenceTree(QString fileName, QString text)
{
    QString s = this->shortFileName(fileName);
    basicPath = fileName.mid(0,fileName.lastIndexOf('/')+1);

    if(referenceModel != NULL) delete referenceModel;
    referenceModel = new TreeModel(s, this);
    if(text.length() > 0) {
        referenceModel->addFileReferences(fileName, xBasicIncludes, xBasicSeparator, text, true);
    }
    referenceTree->setWindowTitle(s);
    referenceTree->setModel(referenceModel);
    referenceTree->show();
}

void MainWindow::setEditorTab(int num, QString shortName, QString fileName, QString text)
{
    QPlainTextEdit *editor = editors->at(num);
    fileChangeDisable = true;
    editor->setPlainText(text);
    fileChangeDisable = false;
    editorTabs->setTabText(num,shortName);
    editorTabs->setTabToolTip(num,fileName);
    editorTabs->setCurrentIndex(num);
}

void MainWindow::checkConfigSerialPort()
{
    if(!cbPort->count()) {
        enumeratePorts();
    } else {
        QString name = cbPort->currentText();
        QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
        int index = -1;
        for (int i = 0; i < ports.size(); i++) {
            if(ports.at(i).portName.contains(name, Qt::CaseInsensitive)) {
                index = i;
                break;
            }
        }
        if(index < 0) {
            enumeratePorts();
        }
    }
}

void MainWindow::enumeratePortsEvent()
{
    enumeratePorts();
    int len = this->cbPort->count();

    // need to check if the port we are using disappeared.
    if(this->btnConnected->isChecked()) {
        bool notFound = true;
        QString plPortName = this->term->getPortName();
        for(int n = this->cbPort->count()-1; n > -1; n--) {
            QString name = cbPort->itemText(n);
            if(!name.compare(plPortName)) {
                notFound = false;
            }
        }
        if(notFound) {
            btnConnected->setChecked(false);
            connectButton();
        }
    }
    else if(len > 1) {
        this->cbPort->showPopup();
    }

}

void MainWindow::enumeratePorts()
{
    if(cbPort != NULL) cbPort->clear();
    QList<QextPortInfo> ports = QextSerialEnumerator::getPorts();
    QStringList stringlist;
    QString name;
    stringlist << "List of ports:";
#ifdef AUTOPORT
    int index = -1;
    progress->setValue(0);
    progress->setVisible(true);
#endif
    for (int i = 0; i < ports.size(); i++) {
        progress->setValue(i*100/ports.size());
        stringlist << "port name:" << ports.at(i).portName;
        stringlist << "friendly name:" << ports.at(i).friendName;
        stringlist << "physical name:" << ports.at(i).physName;
        stringlist << "enumerator name:" << ports.at(i).enumName;
        stringlist << "vendor ID:" << QString::number(ports.at(i).vendorID, 16);
        stringlist << "product ID:" << QString::number(ports.at(i).productID, 16);
        stringlist << "===================================";
#if defined(Q_WS_WIN32)
        if(name.contains(QString("LPT"),Qt::CaseInsensitive) == false) {
            name = ports.at(i).portName;
            cbPort->addItem(name);
        }
#elif defined(Q_WS_MAC)
        name = ports.at(i).portName;
        if(name.indexOf("usbserial",0,Qt::CaseInsensitive) > -1)
            cbPort->addItem(name);
#else
        name = "/"+ports.at(i).physName;
        if(name.indexOf("usb",0,Qt::CaseInsensitive) > -1)
            cbPort->addItem(name);
#endif
#ifdef AUTOPORT
        index++;
        /* test for valid port */
        if(name.length() && !portListener->port->isOpen()) {
            portListener->init(name, BAUD115200);  // signals get hooked up internally
            if(portListener->open()) {
                portListener->close();
                PropellerID propid;
                if(!propid.isDevice(name)) {
                    cbPort->removeItem(index);
                    index--;
                }
            }
            else {
                cbPort->removeItem(index);
                index--;
            }
        }
#endif
    }
#ifdef AUTOPORT
    progress->setValue(100);
#endif
}

void MainWindow::connectButton()
{
    if(btnConnected->isChecked()) {
        btnConnected->setDisabled(true);
        portListener->open();
        btnConnected->setDisabled(false);
        term->setPortName(portListener->port->portName());
        term->show();
    }
    else {
        portListener->close();
        term->hide();
    }
}

QString MainWindow::shortFileName(QString fileName)
{
    QString rets;
    if(fileName.indexOf('/') > -1)
        rets = fileName.mid(fileName.lastIndexOf('/')+1);
    else if(fileName.indexOf('\\') > -1)
        rets = fileName.mid(fileName.lastIndexOf('\\')+1);
    else
        rets = fileName.mid(fileName.lastIndexOf(xBasicSeparator)+1);
    return rets;
}

void MainWindow::initBoardTypes()
{
    cbBoard->clear();

    QFile file;
    if(file.exists(xBasicCfgFile))
    {
        /* load boards in case there were changes */
        xBasicConfig->loadBoards(xBasicCfgFile);
    }

    /* get board types */
    QStringList boards = xBasicConfig->getBoardNames();
    for(int n = 0; n < boards.count(); n++)
        cbBoard->addItem(boards.at(n));

    QVariant lastboardv = settings->value(lastBoardNameKey);
    /* read last board/port to make user happy */
    if(lastboardv.canConvert(QVariant::String))
        boardName = lastboardv.toString();

    /* setup the first board displayed in the combo box */
    if(cbBoard->count() > 0) {
        int ndx = 0;
        if(boardName.length() != 0) {
            for(int n = cbBoard->count()-1; n > -1; n--)
                if(cbBoard->itemText(n) == boardName)
                {
                    ndx = n;
                    break;
                }
        }
        setCurrentBoard(ndx);
    }
}

void MainWindow::setupEditor()
{
    //editor->clear();
    QFont font;
    font.setFamily("Courier");
    font.setFixedPitch(true);
    font.setPointSize(10);

    QPlainTextEdit *editor = new QPlainTextEdit;
    editor->setFont(font);
    editor->setTabStopWidth(font.pointSize()*3);
    connect(editor,SIGNAL(textChanged()),this,SLOT(fileChanged()));
    highlighter = new Highlighter(editor->document());
    editors->append(editor);
}

void MainWindow::setupFileMenu()
{
    QMenu *fileMenu = new QMenu(tr("&File"), this);
    menuBar()->addMenu(fileMenu);

    fileMenu->addAction(QIcon(":/images/newfile.png"), tr("&New"), this, SLOT(newFile()),
                        QKeySequence::New);

    fileMenu->addAction(QIcon(":/images/openfile.png"), tr("&Open"), this, SLOT(openFile()),
                        QKeySequence::Open);

    fileMenu->addAction(QIcon(":/images/savefile.png"), tr("&Save"), this, SLOT(saveFile()),
                        QKeySequence::Save);

    fileMenu->addAction(QIcon(":/images/saveasfile.png"), tr("Save &As"), this, SLOT(saveAsFile()),
                        QKeySequence::SaveAs);

    // fileMenu->addAction(QIcon(":/images/print.png"), tr("Print"), this, SLOT(printFile()), QKeySequence::Print);
    // fileMenu->addAction(QIcon(":/images/zip.png"), tr("Archive"), this, SLOT(zipFile()), 0);

    fileMenu->addAction(QIcon(":/images/exit.png"), tr("E&xit"), qApp, SLOT(quit()),
                        QKeySequence::Quit);

    QMenu *projMenu = new QMenu(tr("&Project"), this);
    menuBar()->addMenu(projMenu);

    //projMenu->addAction(QIcon(":/images/treeproject.png"), tr("Set Project"), this, SLOT(setProject()), Qt::Key_F4);
    projMenu->addAction(QIcon(":/images/properties.png"), tr("Properties"), this, SLOT(properties()), Qt::Key_F5);
    projMenu->addAction(QIcon(":/images/hardware.png"), tr("Configuration"), this, SLOT(hardware()), Qt::Key_F6);

    QMenu *debugMenu = new QMenu(tr("&Debug"), this);
    menuBar()->addMenu(debugMenu);

    debugMenu->addAction(QIcon(":/images/debug.png"), tr("Debug"), this, SLOT(programDebug()), Qt::Key_F8);
    debugMenu->addAction(QIcon(":/images/build2.png"), tr("Build"), this, SLOT(programBuild()), Qt::Key_F9);
    debugMenu->addAction(QIcon(":/images/run.png"), tr("Run"), this, SLOT(programRun()), Qt::Key_F10);
    debugMenu->addAction(QIcon(":/images/burnee.png"), tr("Burn"), this, SLOT(programBurnEE()), Qt::Key_F11);
}

void MainWindow::setupProjectTools(QSplitter *vsplit)
{
    /* container for project, etc... */
    leftSplit = new QSplitter(this);
    leftSplit->setMinimumHeight(500);
    leftSplit->setOrientation(Qt::Vertical);
    vsplit->addWidget(leftSplit);

    /* project tree */
    projectTree = new QTreeView(this);
    projectTree->setMinimumWidth(200);
    projectTree->setMinimumHeight(100);
    projectTree->setMaximumHeight(200);
    projectTree->setMaximumWidth(250);
    connect(projectTree,SIGNAL(clicked(QModelIndex)),this,SLOT(projectTreeClicked(QModelIndex)));
    projectTree->setToolTip(tr("Current Project"));
    leftSplit->addWidget(projectTree);

    /* project reference tree */
    referenceTree = new QTreeView(this);
    referenceTree->setMinimumHeight(300);
    referenceTree->setMaximumWidth(250);
    QFont font = referenceTree->font();
    font.setItalic(true);   // pretty and matches def name font
    referenceTree->setFont(font);
    referenceTree->setToolTip(tr("Project References"));
    connect(referenceTree,SIGNAL(clicked(QModelIndex)),this,SLOT(referenceTreeClicked(QModelIndex)));
    leftSplit->addWidget(referenceTree);

    /* project editors */
    editors = new QVector<QPlainTextEdit*>();

    /* project editor tabs */
    editorTabs = new QTabWidget(this);
    editorTabs->setTabsClosable(true);
    editorTabs->setMinimumWidth(550);
    connect(editorTabs,SIGNAL(tabCloseRequested(int)),this,SLOT(closeTab(int)));
    connect(editorTabs,SIGNAL(currentChanged(int)),this,SLOT(changeTab(int)));
    vsplit->addWidget(editorTabs);
}

void MainWindow::setupToolBars()
{
    fileToolBar = addToolBar(tr("File"));
    QToolButton *btnFileNew = new QToolButton(this);
    QToolButton *btnFileOpen = new QToolButton(this);
    QToolButton *btnFileSave = new QToolButton(this);
    QToolButton *btnFileSaveAs = new QToolButton(this);
    //QToolButton *btnFilePrint = new QToolButton(this);
    //QToolButton *btnFileZip = new QToolButton(this);

    addToolButton(fileToolBar, btnFileNew, QString(":/images/newfile.png"));
    addToolButton(fileToolBar, btnFileOpen, QString(":/images/openfile.png"));
    addToolButton(fileToolBar, btnFileSave, QString(":/images/savefile.png"));
    addToolButton(fileToolBar, btnFileSaveAs, QString(":/images/saveasfile.png"));
    //addToolButton(fileToolBar, btnFilePrint, QString(":/images/print.png"));
    //addToolButton(fileToolBar, btnFileZip, QString(":/images/zip.png"));

    connect(btnFileNew,SIGNAL(clicked()),this,SLOT(newFile()));
    connect(btnFileOpen,SIGNAL(clicked()),this,SLOT(openFile()));
    connect(btnFileSave,SIGNAL(clicked()),this,SLOT(saveFile()));
    connect(btnFileSaveAs,SIGNAL(clicked()),this,SLOT(saveAsFile()));
    //connect(btnFilePrint,SIGNAL(clicked()),this,SLOT(printFile()));
    //connect(btnFileZip,SIGNAL(clicked()),this,SLOT(zipFile()));

    btnFileNew->setToolTip(tr("New"));
    btnFileOpen->setToolTip(tr("Open"));
    btnFileSave->setToolTip(tr("Save"));
    btnFileSaveAs->setToolTip(tr("Save As"));
    //btnFilePrint->setToolTip(tr("Print"));
    //btnFileZip->setToolTip(tr("Archive"));

    propToolBar = addToolBar(tr("Properties"));

    /*
    QToolButton *btnProjectApp = new QToolButton(this);
    addToolButton(propToolBar, btnProjectApp, QString(":/images/treeproject.png"));
    connect(btnProjectApp,SIGNAL(clicked()),this,SLOT(setProject()));
    btnProjectApp->setToolTip(tr("Set Project File"));
    */

    QToolButton *btnProjectProperties = new QToolButton(this);
    addToolButton(propToolBar, btnProjectProperties, QString(":/images/properties.png"));
    connect(btnProjectProperties,SIGNAL(clicked()),this,SLOT(properties()));
    btnProjectProperties->setToolTip(tr("Properties"));

    QToolButton *btnProjectBoard = new QToolButton(this);
    addToolButton(propToolBar, btnProjectBoard, QString(":/images/hardware.png"));
    connect(btnProjectBoard,SIGNAL(clicked()),this,SLOT(hardware()));
    btnProjectBoard->setToolTip(tr("Configuration"));

    debugToolBar = addToolBar(tr("Debug"));
    QToolButton *btnDebugDebugTerm = new QToolButton(this);
    QToolButton *btnDebugRun = new QToolButton(this);
    QToolButton *btnDebugBuild = new QToolButton(this);
    QToolButton *btnDebugBurnEEP = new QToolButton(this);

    addToolButton(debugToolBar, btnDebugBuild, QString(":/images/build2.png"));
    addToolButton(debugToolBar, btnDebugBurnEEP, QString(":/images/burnee.png"));
    addToolButton(debugToolBar, btnDebugRun, QString(":/images/run.png"));
    addToolButton(debugToolBar, btnDebugDebugTerm, QString(":/images/debug.png"));

    connect(btnDebugBuild,SIGNAL(clicked()),this,SLOT(programBuild()));
    connect(btnDebugBurnEEP,SIGNAL(clicked()),this,SLOT(programBurnEE()));
    connect(btnDebugDebugTerm,SIGNAL(clicked()),this,SLOT(programDebug()));
    connect(btnDebugRun,SIGNAL(clicked()),this,SLOT(programRun()));

    btnDebugBuild->setToolTip(tr("Build"));
    btnDebugBurnEEP->setToolTip(tr("Burn EEPROM"));
    btnDebugDebugTerm->setToolTip(tr("Debug"));
    btnDebugRun->setToolTip(tr("Run"));

    ctrlToolBar = addToolBar(tr("Control"));
    ctrlToolBar->setLayoutDirection(Qt::RightToLeft);
    cbBoard = new QComboBox(this);
    cbPort = new QComboBox(this);
    cbBoard->setLayoutDirection(Qt::LeftToRight);
    cbPort->setLayoutDirection(Qt::LeftToRight);
    cbBoard->setToolTip(tr("Board Type Select"));
    cbPort->setToolTip(tr("Serial Port Select"));
    cbBoard->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    cbPort->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(cbBoard,SIGNAL(currentIndexChanged(int)),this,SLOT(setCurrentBoard(int)));
    connect(cbPort,SIGNAL(currentIndexChanged(int)),this,SLOT(setCurrentPort(int)));

    btnConnected = new QToolButton(this);
    btnConnected->setToolTip(tr("Port Status"));
    btnConnected->setCheckable(true);
    connect(btnConnected,SIGNAL(clicked()),this,SLOT(connectButton()));

    QToolButton *btnPortScan = new QToolButton(this);
    btnPortScan->setToolTip(tr("Rescan Serial Ports"));
    connect(btnPortScan,SIGNAL(clicked()),this,SLOT(enumeratePorts()));

    /* this could be a refresh hardware toolbar button ...
     * the feature is automatic after hardware dialog OK now.
    QToolButton *btnConfigScan = new QToolButton(this);
    btnConfigScan->setToolTip(tr("Rescan Board Configurations"));
    connect(btnConfigScan,SIGNAL(clicked()),this,SLOT(initBoardTypes()));
    */

    addToolButton(ctrlToolBar, btnConnected, QString(":/images/connected2.png"));
    addToolButton(ctrlToolBar, btnPortScan, QString(":/images/refresh.png"));
    ctrlToolBar->addWidget(cbPort);
    //addToolButton(ctrlToolBar, btnConfigScan, QString(":/images/refresh.png"));
    ctrlToolBar->addWidget(cbBoard);
    ctrlToolBar->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
}

