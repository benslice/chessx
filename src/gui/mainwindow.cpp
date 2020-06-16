/***************************************************************************
 *   (C) 2005-2009 by Michal Rudolf mrudolf@kdewebdev.org                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "analysiswidget.h"
#include "boardsetup.h"
#include "boardview.h"
#include "boardviewex.h"
#include "chartwidget.h"
#include "clipboarddatabase.h"
#include "commentdialog.h"
#include "copydialog.h"
#include "databaseinfo.h"
#include "databaselist.h"
#include "databaselistmodel.h"
#include "dockwidgetex.h"
#include "downloadmanager.h"
#include "ecolistwidget.h"
#include "ecothread.h"
#include "eventlistwidget.h"
#include "exclusiveactiongroup.h"
#include "ficsclient.h"
#include "ficsconsole.h"
#include "ficsdatabase.h"
#include "gamex.h"
#include "gamelist.h"
#include "GameMimeData.h"
#include "gamewindow.h"
#include "gametoolbar.h"
#include "gamenotationwidget.h"
#include "helpbrowser.h"
#include "helpbrowsershell.h"
#include "historylabel.h"
#include "kbaction.h"
#include "loadquery.h"
#include "mainwindow.h"
#include "messagedialog.h"
#include "memorydatabase.h"
#include "openingtreewidget.h"
#include "output.h"
#include "pgndatabase.h"
#include "playerlistwidget.h"
#include "quazip.h"
#include "quazipfile.h"
#include "savedialog.h"
#include "settings.h"
#include "style.h"
#include "tableview.h"
#include "tagdialog.h"
#include "tags.h"
#include "textedit.h"
#include "toolmainwindow.h"
#include "translatingslider.h"
#include "version.h"

#include <time.h>

#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QLCDNumber>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProgressBar>
#include <QSizePolicy>
#ifdef USE_SOUND
#include <QSound>
#endif
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#ifdef USE_SPEECH
#include <QTextToSpeech>
#endif
#include <QTimer>
#include <QToolBar>

#if defined(_MSC_VER) && defined(_DEBUG)
#define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__ )
#define new DEBUG_NEW
#endif // _MSC_VER

MainWindow::MainWindow() : QMainWindow(),
    m_tabDragIndex(-1),
    m_pDragTabBar(nullptr),
    m_gameWindow(nullptr),
    m_gameToolBar(0),
    m_operationFlag(0),
    m_currentFrom(InvalidSquare),
    m_currentTo(InvalidSquare),
    m_lastColor('G'),
    m_machineHasToMove(false),
    m_gameMode(false),
    m_scratchPad(nullptr),
    m_bEvalRequested(false),
    m_lastMessageWasHint(false),
    m_readAhead(0)
{
    setObjectName("MainWindow");
    m_registry = new DatabaseRegistry();

    // Style::setStyle(this);
    m_messageTimer = new QTimer(this);
    m_messageTimer->setInterval(5000);
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, SIGNAL(timeout()), this, SLOT(slotStatusMessage()));

    m_autoPlayTimer = new QTimer(this);
    m_autoPlayTimer->setInterval(AppSettings->getValue("/Board/AutoPlayerInterval").toInt());
    m_autoPlayTimer->setSingleShot(true);
    connect(m_autoPlayTimer, SIGNAL(timeout()), this, SLOT(slotAutoPlayTimeout()));

    m_dragTimer = new QTimer(this);
    m_dragTimer->setSingleShot(true);
    connect(m_dragTimer, SIGNAL(timeout()), this, SLOT(slotAutoSwitchTab()));

    /* Create clipboard database */
    DatabaseInfo* pClipDB = new DatabaseInfo(&m_undoGroup, new ClipboardDatabase);
    connect(pClipDB,SIGNAL(signalRestoreState(GameX)), SLOT(slotDbRestoreState(GameX)));
    connect(pClipDB,SIGNAL(signalGameModified(bool)), SLOT(slotGameChanged(bool)));
    connect(pClipDB,SIGNAL(signalMoveChanged()), SLOT(slotMoveChanged()));
    connect(pClipDB,SIGNAL(signalGameModified(bool)), SIGNAL(signalGameModified(bool)));
    m_registry->m_databases.append(pClipDB);
    m_currentDatabase = pClipDB;

    /* Game List */
    DockWidgetEx* gameListDock = new DockWidgetEx(tr("Game List"), this);
    gameListDock->setObjectName("GameList");
    m_gameList = new GameList(databaseInfo()->filter(), gameListDock);

    // Init before setupActions
    m_ficsClient = new FicsClient(this);
    m_ficsConsole = new FicsConsole(this, m_ficsClient);

    /* Actions */
    m_gameModeGroup = new QActionGroup(this);
    connect(this, SIGNAL(signalGameModeChanged(bool)), m_gameModeGroup, SLOT(setDisabled(bool)));
    setupActions();

    /* Delete on close */
    setAttribute(Qt::WA_DeleteOnClose);

    /* Board */
    m_boardSplitter = new QSplitter(Qt::Horizontal);
    m_boardSplitter->setChildrenCollapsible(false);
    setCentralWidget(m_boardSplitter);
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("TabWidget");
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setElideMode(Qt::ElideNone);
    m_tabWidget->setUsesScrollButtons(true);
    connect(m_tabWidget, SIGNAL(tabCloseRequested(int)), SLOT(slotCloseTabWidget(int)));
    connect(m_tabWidget, SIGNAL(tabBarClicked(int)), SLOT(slotActivateBoardView(int)));
    /* Board layout */
    m_boardSplitter->addWidget(m_tabWidget);

    DockWidgetEx* ficsConsoleDock = new DockWidgetEx(tr("FICS Console"), this);
    ficsConsoleDock->setObjectName("FicsCOnsoleDock");
    ficsConsoleDock->setMinimumSize(150, 100);
    ficsConsoleDock->setWidget(m_ficsConsole);
    ficsConsoleDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, ficsConsoleDock);
    connect(m_ficsConsole, SIGNAL(raiseRequest()), ficsConsoleDock, SLOT(show()));
    connect(m_ficsConsole, SIGNAL(raiseRequest()), ficsConsoleDock, SLOT(raise()));
    connect(m_ficsConsole, SIGNAL(ReceivedBoard(int,QString)), this, SLOT(HandleFicsBoardRequest(int,QString)));
    connect(m_ficsConsole, SIGNAL(SignalGameResult(QString)), this, SLOT(HandleFicsResultRequest(QString)));
    connect(m_ficsConsole, SIGNAL(RequestNewGame()), this, SLOT(HandleFicsNewGameRequest()));
    connect(m_ficsConsole, SIGNAL(FicsShowTimer(bool)), this, SLOT(SlotShowTimer(bool)));
    connect(m_ficsConsole, SIGNAL(FicsShowTime(int, QString)), this, SLOT(SlotDisplayTime(int,QString)));
    connect(m_ficsConsole, SIGNAL(RequestSaveGame()), this, SLOT(HandleFicsSaveGameRequest()));
    connect(m_ficsConsole, SIGNAL(RequestCloseFICS()), this, SLOT(HandleFicsCloseRequest()));
    connect(m_ficsConsole, SIGNAL(RequestAddTag(QString,QString)), this, SLOT(HandleFicsAddTagRequest(QString,QString)));
    connect(m_ficsConsole, SIGNAL(RequestGameMode(bool)), this, SLOT(enterGameMode(bool)));
    connect(m_ficsConsole, SIGNAL(RequestRemoveLastMove()), this, SLOT(HandleFicsRequestRemoveMove()));
    connect(m_ficsClient, SIGNAL(disconnected()), SLOT(FicsDisconnected()), Qt::QueuedConnection);
    connect(m_ficsClient, SIGNAL(connected()), SLOT(FicsConnected()), Qt::QueuedConnection);
    connect(m_ficsConsole, SIGNAL(RequestStoredMove()), SLOT(slotBoardStoredMove()));
    connect(this, SIGNAL(reconfigure()), m_ficsConsole, SLOT(slotReconfigure()));
    m_ficsConsole->setEnabled(false);

    /* Game view */
    DockWidgetEx* gameTextDock = new DockWidgetEx(tr("Notation"), this);
    gameTextDock->setObjectName("GameTextDock");

    m_gameWindow = new GameWindow(gameTextDock);
    connect(this, SIGNAL(reconfigure()), m_gameWindow, SLOT(slotReconfigure()));
    m_gameToolBar = new GameToolBar(tr("Game Time"), m_gameWindow);
    m_gameToolBar->setMovable(false);
    m_gameWindow->addToolBar(Qt::BottomToolBarArea, m_gameToolBar);
    connect(m_gameToolBar, &GameToolBar::requestPly, this, &MainWindow::slotGameMoveToPly);

    m_menuView->addAction(m_gameToolBar->toggleViewAction());
    m_gameToolBar->setVisible(AppSettings->getValue("/MainWindow/GameToolBar").toBool());
    m_gameView = m_gameWindow->browser();
    connect(m_gameView, &GameNotationWidget::anchorClicked, this, &MainWindow::slotGameViewLinkUrl);
    connect(m_gameView, &GameNotationWidget::actionRequested, this, &MainWindow::slotGameModify);
    connect(m_gameView, &GameNotationWidget::queryActiveGame, this, &MainWindow::slotGetActiveGame);
    connect(m_gameView, &GameNotationWidget::signalMergeGame, this, &MainWindow::slotMergeActiveGame);
    connect(this, SIGNAL(signalGameLoaded(const BoardX&)), gameTextDock, SLOT(raise()));
    gameTextDock->setWidget(m_gameWindow);
    connect(this, &MainWindow::reconfigure, m_gameView, &GameNotationWidget::slotReconfigure);
    addDockWidget(Qt::RightDockWidgetArea, gameTextDock);
    m_gameTitle = new QLabel;
    connect(m_gameTitle, SIGNAL(linkActivated(QString)), this, SLOT(slotGameViewLink(QString)));
    gameTextDock->setTitleBarWidget(m_gameTitle);

    m_menuView->addAction(gameTextDock->toggleViewAction());
    gameTextDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::Key_E);

    /* Game List */
    m_gameList->setMinimumSize(150, 100);
    connect(m_gameList, SIGNAL(gameSelected(GameId)), SLOT(slotFilterLoad(GameId)));
    connect(m_gameList, SIGNAL(requestCopyGame(QList<GameId>)), SLOT(slotDatabaseCopy(QList<GameId>)));
    connect(m_gameList, SIGNAL(requestFindDuplicates(QList<GameId>)), SLOT(slotDatabaseFindDuplicates(QList<GameId>)));
    connect(m_gameList, SIGNAL(requestMergeGame(QList<GameId>)), SLOT(slotMergeActiveGameList(QList<GameId>)));
    connect(m_gameList, SIGNAL(requestMergeAllGames()), SLOT(slotMergeAllGames()));
    connect(m_gameList, SIGNAL(requestMergeFilter()), SLOT(slotMergeFilter()));
    connect(m_gameList, SIGNAL(requestDeleteGame(QList<GameId>)), SLOT(slotDatabaseDeleteGame(QList<GameId>)));
    connect(m_gameList, SIGNAL(requestGameData(GameX&)), SLOT(slotGetGameData(GameX&)));
    connect(m_gameList, SIGNAL(searchProgress(int)), SLOT(slotBoardSearchUpdate(int)));
    connect(m_gameList, SIGNAL(searchFinished()), SLOT(slotBoardSearchFinished()));

    connect(this, SIGNAL(reconfigure()), m_gameList, SLOT(slotReconfigure()));
    gameListDock->setWidget(m_gameList);
    m_menuView->addAction(gameListDock->toggleViewAction());
    gameListDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::Key_L);
    connect(m_gameList, SIGNAL(raiseRequest()), gameListDock, SLOT(raise()));

    // Player List
    DockWidgetEx* playerListDock = new DockWidgetEx(tr("Players"), this);
    playerListDock->setObjectName("PlayerList");
    m_playerList = new PlayerListWidget(this);
    m_playerList->setMinimumSize(150, 100);
    playerListDock->setWidget(m_playerList);
    // addDockWidget(Qt::RightDockWidgetArea, playerListDock);
    m_menuView->addAction(playerListDock->toggleViewAction());
    playerListDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::ALT + Qt::Key_P);
    connect(m_playerList, SIGNAL(filterRequest(QString)), m_gameList, SLOT(slotFilterListByPlayer(QString)));
    connect(m_playerList, SIGNAL(renameRequest(QString)), SLOT(slotRenamePlayer(QString)));
    connect(m_playerList, SIGNAL(filterEcoPlayerRequest(QString, QString, QString, QString)), m_gameList, SLOT(slotFilterListByEcoPlayer(QString, QString, QString, QString)));
    connect(this, SIGNAL(databaseChanged(DatabaseInfo*)), m_playerList, SLOT(setDatabase(DatabaseInfo*)));
    connect(this, SIGNAL(reconfigure()), m_playerList, SLOT(slotReconfigure()));
    // playerListDock->hide();

    // Event List
    DockWidgetEx* eventListDock = new DockWidgetEx(tr("Events"), this);
    eventListDock->setObjectName("EventList");
    m_eventList = new EventListWidget(this);
    m_eventList->setMinimumSize(150, 100);
    eventListDock->setWidget(m_eventList);
    m_menuView->addAction(eventListDock->toggleViewAction());
    eventListDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_P);
    connect(m_eventList, SIGNAL(filterRequest(QString)), m_gameList, SLOT(slotFilterListByEvent(QString)));
    connect(m_eventList, SIGNAL(renameRequest(QString)), SLOT(slotRenameEvent(QString)));
    connect(m_eventList, SIGNAL(filterEventPlayerRequest(QString, QString)), m_gameList, SLOT(slotFilterListByEventPlayer(QString, QString)));
    connect(m_eventList, SIGNAL(filterEventPlayerRequest(QString, QString)), m_playerList, SLOT(slotSelectPlayer(QString)));
    connect(this, SIGNAL(databaseChanged(DatabaseInfo*)), m_eventList, SLOT(setDatabase(DatabaseInfo*)));
    connect(this, SIGNAL(reconfigure()), m_eventList, SLOT(slotReconfigure()));
    eventListDock->hide();

    // ECO List
    DockWidgetEx* ecoListDock = new DockWidgetEx(tr("ECO"), this);
    ecoListDock->setObjectName("EcoList");
    m_ecoList = new ECOListWidget(this);
    m_ecoList->setMinimumSize(150, 100);
    ecoListDock->setWidget(m_ecoList);
    m_menuView->addAction(ecoListDock->toggleViewAction());
    ecoListDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::ALT + Qt::Key_E);
    connect(m_ecoList, SIGNAL(filterRequest(QString)), m_gameList, SLOT(slotFilterListByEco(QString)));
    connect(m_ecoList, SIGNAL(filterEcoPlayerRequest(QString, QString, QString, QString)), m_gameList, SLOT(slotFilterListByEcoPlayer(QString, QString, QString, QString)));
    connect(m_ecoList, SIGNAL(filterEcoPlayerRequest(QString, QString)), m_playerList, SLOT(slotSelectPlayer(QString)));
    connect(this, SIGNAL(databaseChanged(DatabaseInfo*)), m_ecoList, SLOT(setDatabase(DatabaseInfo*)));
    connect(this, SIGNAL(reconfigure()), m_ecoList, SLOT(slotReconfigure()));
    ecoListDock->hide();

    // Database List
    DockWidgetEx* dbListDock = new DockWidgetEx(tr("Databases"), this);
    dbListDock->setObjectName("Databases");
    m_databaseList = new DatabaseList(m_registry, this);
    m_databaseList->setMinimumSize(150, 100);
    dbListDock->setWidget(m_databaseList);
    // addDockWidget(Qt::RightDockWidgetArea, dbListDock);
    m_menuView->addAction(dbListDock->toggleViewAction());
    dbListDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::Key_D);
    connect(m_databaseList, SIGNAL(requestOpenDatabase(QString, bool)),
            this, SLOT(openDatabaseUrl(QString, bool)));
    connect(m_databaseList, SIGNAL(requestCloseDatabase(QString)),
            this, SLOT(slotFileCloseName(QString)));
    connect(m_databaseList, SIGNAL(requestLinkDatabase(QString)),
            this, SLOT(setFavoriteDatabase(QString)));
    connect(m_databaseList, SIGNAL(requestAppendGames(QString, QList<GameId>, QString)),
            this, SLOT(copyGames(QString, QList<GameId>, QString)));
    connect(m_gameList, SIGNAL(requestAppendGames(QString, QList<GameId>, QString)),
            this, SLOT(copyGames(QString, QList<GameId>, QString)));
    connect(m_databaseList, SIGNAL(requestAppendDatabase(QString, QString)),
            this, SLOT(copyDatabase(QString, QString)));
    connect(this, SIGNAL(reconfigure()), m_databaseList, SLOT(slotReconfigure()));
    connect(m_databaseList, SIGNAL(requestMakeBook(QString)),
            this, SLOT(slotMakeBook(QString)));

    m_databaseList->addFileOpen(pClipDB->database()->name(), false);
    m_databaseList->setFileCurrent(pClipDB->database()->name());

    restoreRecentFiles();
    connect(m_databaseList, SIGNAL(raiseRequest()), dbListDock, SLOT(raise()));
    connect(this, SIGNAL(signalGameModeChanged(bool)), m_databaseList, SLOT(setDisabled(bool)));

    /* Create FICS database */
    setFavoriteDatabase(ficsPath());

    /* Recent files */
    m_recentFiles.restore();
    m_recentFiles.removeMissingFiles();

    /* Opening Tree */
    DockWidgetEx* openingDock = new DockWidgetEx(tr("Opening Tree"), this);
    openingDock->setObjectName("OpeningTreeDock");
    m_openingTreeWidget = new OpeningTreeWidget(this);
    openingDock->setWidget(m_openingTreeWidget);
    addDockWidget(Qt::RightDockWidgetArea, openingDock);
    m_menuView->addAction(openingDock->toggleViewAction());
    connect(openingDock->toggleViewAction(), SIGNAL(triggered()), SLOT(slotSearchTree()));
    connect(openingDock, SIGNAL(visibilityChanged(bool)), m_openingTreeWidget, SLOT(cancel()));
    connect(m_openingTreeWidget, SIGNAL(signalTreeUpdated(bool)), this, SLOT(slotTreeUpdate(bool)));
    connect(m_openingTreeWidget, SIGNAL(signalSourceChanged()), this, SLOT(slotSearchTree()));
    connect(m_openingTreeWidget, SIGNAL(requestGameFilterUpdate(int,int)), this, SLOT(slotGameFilterUpdate(int,int)));

    connect(this, SIGNAL(reconfigure()), m_openingTreeWidget, SLOT(slotReconfigure()));
    openingDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::Key_T);
    openingDock->hide();
    connect(this, SIGNAL(signalDatabaseOpenClose()), this, SLOT(slotUpdateOpeningTreeWidget()));
    connect(this, SIGNAL(signalGameModeChanged(bool)), m_openingTreeWidget, SLOT(setDisabled(bool)));
    connect(this, SIGNAL(signalUpdateDatabaseList(QStringList)), m_openingTreeWidget, SLOT(updateFilterIndex(QStringList)));

    // Arrange Upper Rightside docks
    tabifyDockWidget(ficsConsoleDock, eventListDock);
    tabifyDockWidget(ficsConsoleDock, ecoListDock);
    tabifyDockWidget(ficsConsoleDock, playerListDock);
    tabifyDockWidget(ficsConsoleDock, openingDock);
    tabifyDockWidget(ficsConsoleDock, dbListDock);

    // Arrange Lower Rightside docks
    tabifyDockWidget(gameTextDock, gameListDock);

    /* Analysis Dock */
    DockWidgetEx* analysisDock = new DockWidgetEx(tr("Analysis 1"), this);
    analysisDock->setObjectName("AnalysisDock1");   
    analysisDock->toggleViewAction()->setShortcut(Qt::CTRL + Qt::Key_F2);
    m_mainAnalysis = new AnalysisWidget(this);
    m_mainAnalysis->setObjectName("Analysis");
    setupAnalysisWidget(analysisDock, m_mainAnalysis);
    addDockWidget(Qt::LeftDockWidgetArea, analysisDock);

    /* Analysis Dock 2 */
    DockWidgetEx* analysisDock2 = new DockWidgetEx(tr("Analysis 2"), this);
    analysisDock2->setObjectName("AnalysisDock2");
    analysisDock2->toggleViewAction()->setShortcut(Qt::CTRL + Qt::Key_F3);
    m_secondaryAnalysis = new AnalysisWidget(this);
    m_secondaryAnalysis->setObjectName("Analysis2");
    setupAnalysisWidget(analysisDock2, m_secondaryAnalysis);
    addDockWidget(Qt::LeftDockWidgetArea, analysisDock2);

    /* Randomize */
    srand(time(nullptr));

    /* Append the FICS console to the view menu */
    m_menuView->addAction(ficsConsoleDock->toggleViewAction());

    /* Restoring layouts */
    if(!AppSettings->layout(this))
    {
        resize(1400, 600);
    }
    else
    {
        m_boardSplitter->restoreState(AppSettings->value("/MainWindow/BoardSplit").toByteArray());
    }

    /* Status */
    m_statusFilter = new QLabel();
    statusBar()->addPermanentWidget(m_statusFilter,1);
    m_statusFilter->setFrameStyle(QFrame::Panel | QFrame::Sunken);

    m_statusApp = new HistoryLabel();
    m_statusApp->setMaxSize(128);
    statusBar()->addPermanentWidget(m_statusApp,1);
    m_statusApp->setFrameStyle(QFrame::Panel | QFrame::Sunken);

    QLabel* statusLabel = new QLabel();
    statusBar()->addPermanentWidget(statusLabel,1);
    connect(this, SIGNAL(enterText(QString)), statusLabel, SLOT(setText(QString)));
    statusLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);

    m_sliderSpeed = new TranslatingSlider(this);
    m_sliderSpeed->setMultiplier(1000);
    m_sliderSpeed->setMultiplier2(10000);
    m_sliderSpeed->setMultiplier3(60000);
    m_sliderSpeed->setOrientation(Qt::Horizontal);
    m_sliderSpeed->setMinimum(0);  // O = Infinite
    m_sliderSpeed->setStart2(30);  // Step 10s after 30s
    m_sliderSpeed->setStart3(57);  // Step 60s after 5min
    m_sliderSpeed->setMaximum(97); // 45 Minutes
    m_sliderSpeed->setTranslatedValue(AppSettings->getValue("/Board/AutoPlayerInterval").toInt());
    m_sliderSpeed->setTickInterval(1);
    m_sliderSpeed->setTickPosition(QSlider::NoTicks);
    m_sliderSpeed->setSingleStep(1);
    m_sliderSpeed->setPageStep(1);
    m_sliderSpeed->setMinimumWidth(120); // 87 + some pixel for overlapping slider
    m_sliderSpeed->setMaximumWidth(400); // Arbitrary limit - not really needed

    connect(m_sliderSpeed, SIGNAL(translatedValueChanged(int)), SLOT(slotMoveIntervalChanged(int)));
    connect(m_mainAnalysis, SIGNAL(receivedBestMove(const Analysis&)), this, SLOT(slotEngineTimeout(const Analysis&)));
    connect(m_secondaryAnalysis, SIGNAL(receivedBestMove(const Analysis&)), this, SLOT(slotEngineTimeout(const Analysis&)));

    m_matchParameter.tm           = (EngineParameter::TimeModus) AppSettings->getValue("/Match/Mode").toBool();
    m_matchParameter.ms_totalTime = AppSettings->getValue("/Match/TotalTime").toInt();
    m_matchParameter.ms_increment = AppSettings->getValue("/Match/Increment").toInt();
    m_matchParameter.ms_bonus     = AppSettings->getValue("/Match/UserBonus").toInt();
    m_matchParameter.movesToDo    = AppSettings->getValue("/Match/MoveCount").toInt();
    m_matchParameter.annotateEgt  = AppSettings->getValue("/Match/AnnotateEgt").toBool();
    m_matchParameter.allowBook    = AppSettings->getValue("/Match/AllowBook").toBool();
    m_matchParameter.bookMove     = AppSettings->getValue("/Match/BookMove").toBool();
    m_matchParameter.engineStarts = AppSettings->getValue("/Match/EngineStarts").toBool();
    m_matchParameter.analysisMode = false;

    m_matchParameter.reset();

    m_comboEngine = new QComboBox(this);
    m_comboEngine->addItem(tr("Move Interval:"));
    m_comboEngine->addItem(tr("Search Depth:"));
    statusBar()->addPermanentWidget(m_comboEngine);
    statusBar()->addPermanentWidget(m_sliderSpeed);
    m_sliderText = new QLabel(this);
    slotSetSliderText(0);
    m_sliderText->setFixedWidth(m_sliderText->sizeHint().width());

    statusBar()->addPermanentWidget(m_sliderText);
    connect(m_sliderSpeed, SIGNAL(translatedValueChanged(int)), this, SLOT(slotSetSliderText()));
    connect(m_comboEngine, SIGNAL(currentIndexChanged(int)), this, SLOT(slotSetSliderText()));
    connect(m_comboEngine, SIGNAL(currentIndexChanged(int)), SLOT(slotEngineModeChanged(int)));

    slotSetSliderText();
    setEngineMoveTime();

    statusBar()->setFixedHeight(statusBar()->height());
    statusBar()->setSizeGripEnabled(true);
    m_progressBar = new QProgressBar();

    /* Very late as this will update other widgets */
    connect(this, SIGNAL(databaseModified()), SLOT(slotDatabaseModified()));
    CreateBoardView();

    /* Setup the dimensions of all widgets and the main board */
    slotReconfigure();

    /* Display main window */
    show();
    downloadManager = new DownloadManager(this);
    downloadManager2 = new DownloadManager(this);
    connect(downloadManager2, SIGNAL(onDownloadFinished(QUrl, QString)), this, SLOT(slotDatabaseDroppedHandler(QUrl,QString)), static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    connect(downloadManager2, SIGNAL(downloadError(QUrl)), this, SLOT(slotDatabaseDroppedFailed(QUrl)), static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));

    /* Load files from command line */
    QStringList args = qApp->arguments();
    for(int i = 1; i < args.count(); i++)
    {
        if(QFile::exists(args[i]))
        {
            openDatabaseArchive(args[i], false);
        }
    }

    // Load favorites
    loadFileFavorites();

    qApp->installEventFilter(this);
    /* Activate clipboard */
    slotDatabaseChanged();
    emit signalGameIsEmpty(true);
    emit signalGameLoaded(game().startingBoard());

    setAcceptDrops(true);

    QString dir = AppSettings->indexPath();
    QDir().mkpath(dir);

    /* Load ECO file */
    slotStatusMessage(tr("Loading ECO file..."));
    EcoThread* ecothread = new EcoThread();
    connect(ecothread, SIGNAL(loaded(QObject*, bool)), this, SLOT(ecoLoaded(QObject*, bool)));
    ecothread->start();
    StartCheckUpdate();
#ifdef USE_SPEECH
    qRegisterMetaType<QTextToSpeech::State>("State");
    speech = new QTextToSpeech(this);
    speech->setLocale(QLocale(AppSettings->getValue("/General/language").toString()));
    connect(speech, SIGNAL(stateChanged(QTextToSpeech::State)), SLOT(speechStateChanged(QTextToSpeech::State)), Qt::QueuedConnection);
#endif

    if (isMinimized())
    {
        showNormal();
    }
}

void MainWindow::setupAnalysisWidget(DockWidgetEx* analysisDock, AnalysisWidget* analysis)
{
    analysisDock->setWidget(analysis);
    // addDockWidget(Qt::RightDockWidgetArea, analysisDock);
    connect(analysis, SIGNAL(addVariation(Analysis, QString)),
            SLOT(slotGameAddVariation(Analysis, QString)));
    connect(analysis, SIGNAL(addVariation(QString)),
            SLOT(slotGameAddVariation(QString)));
    connect(this, SIGNAL(boardChange(const BoardX&, const QString&)), analysis, SLOT(setPosition(const BoardX&,QString)));
    connect(this, SIGNAL(reconfigure()), analysis, SLOT(slotReconfigure()));
    // Make sure engine is disabled if dock is hidden
    connect(analysisDock, SIGNAL(visibilityChanged(bool)),
            analysis, SLOT(slotVisibilityChanged(bool)));
    m_menuView->addAction(analysisDock->toggleViewAction());
    analysisDock->hide();
    connect(this, SIGNAL(signalGameLoaded(const BoardX&)), analysis, SLOT(slotUciNewGame(const BoardX&)));
    connect(this, SIGNAL(signalGameModeChanged(bool)), analysis, SLOT(setDisabled(bool)));
    connect(this, SIGNAL(signalUpdateDatabaseList(QStringList)), analysis, SLOT(slotUpdateBooks(QStringList)));
    connect(analysis, SIGNAL(signalSourceChanged(QString)), this, SLOT(slotUpdateOpeningBook(QString)));
    connect(this, SIGNAL(signalGameModeChanged(bool)), analysis, SLOT(setGameMode(bool)));
}

MainWindow::~MainWindow()
{
    m_autoPlayTimer->stop();
    m_dragTimer->stop();
    m_openingTreeWidget->cancel();
    m_ficsConsole->Terminate();

    foreach(DatabaseInfo* dbi, m_registry->databases())
    {
        // Avoid any GUI action if a database is closed
        disconnect(dbi,SIGNAL(signalGameModified(bool)), this, SLOT(slotGameChanged(bool)));
        dbi->close();
    }
    delete m_registry;
    delete m_progressBar;
    delete m_gameList;

    delete autoGroup;

    EcoPositions::terminateEco();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if(event->type() == QEvent::FileOpen)
    {
        openDatabaseUrl(static_cast<QFileOpenEvent*>(event)->file(), false);
        return true;
    }
    else
    {
        if(event->type() == QEvent::KeyPress)
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if(keyEvent && (keyEvent->key() == Qt::Key_Escape ||
                            keyEvent->key() == Qt::Key_Return ||
                            keyEvent->key() == Qt::Key_Enter))
            {
                if (obj == this || obj == m_boardView || obj == m_gameView || obj == m_mainAnalysis || obj == m_secondaryAnalysis)
                {
                    keyPressEvent(keyEvent);
                    return (obj != m_boardView);
                }
            }
        }
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
}

void MainWindow::ecoLoaded(QObject* ecoThread, bool ok)
{
    slotStatusMessage(ok ? tr("ECO Loaded.") : tr("ECO Load Error."));
    qobject_cast<QThread*>(ecoThread)->wait();
    delete ecoThread;
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if(confirmQuit())
    {
        m_recentFiles.save();
        m_databaseList->save();

        m_gameList->saveConfig();
        m_databaseList->saveConfig();
        m_openingTreeWidget->saveConfig();
        m_gameWindow->saveConfig();
        m_scratchPad->saveConfig();
        m_gameView->saveConfig();
        m_ficsConsole->saveConfig();
        m_mainAnalysis->saveConfig();
        m_secondaryAnalysis->saveConfig();
        BoardViewEx* frame = BoardViewFrame(m_boardView);
        frame->saveConfig();

        AppSettings->setLayout(this);
        AppSettings->beginGroup("/MainWindow/");
        AppSettings->setValue("BoardSplit", m_boardSplitter->saveState());
        AppSettings->setValue("GameToolBar", m_gameToolBar->isVisible());
        AppSettings->endGroup();
    }
    else
    {
        e->ignore();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    evaluateSanNag(e);
    emit enterText(m_nagText);
    QWidget::keyPressEvent(e);
}

void MainWindow::evaluateSanNag(QKeyEvent *e)
{
    if(e->key() == Qt::Key_Escape)
    {
        m_nagText.clear();
        return;
    }

    if ((e->key() == Qt::Key_Backspace) && m_nagText.count())
    {
        m_nagText = m_nagText.left(m_nagText.count()-1);
        return;
    }

    if(e->key() == Qt::Key_Delete)
    {
        if(!game().atGameStart())
        {
            game().clearNags();
        }
        m_nagText.clear();
        return;
    }

    if(e->text().isEmpty())
    {
        return;
    }

    bool enterPressed = ((e->key() == Qt::Key_Enter) || (e->key() == Qt::Key_Return));

    if(enterPressed)
    {
        if (m_nagText.isEmpty())
        {
            return;
        }
        // Try to figure out a SAN
        if(NagSet::hasMatch(m_nagText)) // enter forces the current nag
        {
            if(!game().atGameStart())
            {
                if (game().addNag(NagSet::fromString(m_nagText)))
                {
                    m_nagText.clear();
                    return;
                }
                // Could still be move
            }
        }
        if (addVariationFromSan(m_nagText))
        {
            if (qobject_cast<FicsDatabase*>(database()))
            {
                Move m = game().move();
                m_ficsConsole->SendMove(m.toAlgebraic());
            }
        }
        m_nagText.clear(); // Not a move and not a nag
        return;
    }
    else
    {
        m_nagText.append(e->text());
        if (!(m_nagText.length()<=3 && m_nagText.contains("-")) && addVariationFromSan(m_nagText)) // Avoid 0-0 / Nullmove been sent too early
        {
            if (qobject_cast<FicsDatabase*>(database()))
            {
                Move m = game().move();
                m_ficsConsole->SendMove(m.toAlgebraic());
            }
            m_nagText.clear();
            return;
        }
    }

    if (m_nagText == "N")
    {
        // Not a novelty but a Knight
        return;
    }

    if (m_nagText == "n")
    {
        m_nagText = "N";
    }

    if(Nag n = NagSet::uniqueMatch(m_nagText))
    {
        game().addNag(n);
        m_nagText.clear();
    }
}

DatabaseInfo* MainWindow::databaseInfo()
{
    return m_currentDatabase;
}

const DatabaseInfo* MainWindow::databaseInfo() const
{
    return m_currentDatabase;
}

Database* MainWindow::database()
{
    return databaseInfo()->database();
}

const Database* MainWindow::database() const
{
    return databaseInfo()->database();
}

QString MainWindow::databaseName(int index) const
{
    DatabaseInfo* pDbInfo;
    if(index < 0)
    {
        pDbInfo = m_currentDatabase;
    }
    else
    {
        pDbInfo = m_registry->databases().at(index);
    }
    return pDbInfo->dbName();
}

Database* MainWindow::getDatabaseByPath(QString path)
{
    if(DatabaseInfo* dbInfo = getDatabaseInfoByPath(path))
    {
        if(dbInfo->isValid())
        {
            return dbInfo->database();
        }
    }
    return nullptr;
}

DatabaseInfo* MainWindow::getDatabaseInfoByPath(QString path)
{
    return m_registry->findDisplayName(path);
}

GameX& MainWindow::game()
{
    return databaseInfo()->currentGame();
}

const GameX& MainWindow::game() const
{
    return databaseInfo()->currentGame();
}

GameId MainWindow::gameIndex() const
{
    return databaseInfo()->currentIndex();
}

void MainWindow::gameLoad(GameId index)
{
    if (VALID_INDEX(index))
    {
        if(QuerySaveGame())
        {
            if(databaseInfo()->loadGame(index))
            {
                updateLastGameList();
                m_gameList->selectGame(index);
                emit signalGameIsEmpty(false);
                UpdateBoardInformation();
                m_gameList->setFocus();
                emit signalGameLoaded(game().startingBoard());
                if(m_training->isChecked())
                {
                    m_boardView->setFlipped((game().board().toMove() == Black));
                }
                else
                {
                    QString name = AppSettings->getValue("/Board/PlayerTurnBoard").toString();
                    if (!name.isEmpty())
                    {
                        QRegExp re(name);
                        QString nameWhite = game().tag(TagNameWhite);
                        QString nameBlack = game().tag(TagNameBlack);
                        if (nameBlack.indexOf(re) >= 0)
                        {
                            m_boardView->setFlipped(true);
                        }
                        else if (nameWhite.indexOf(re) >= 0)
                        {
                            m_boardView->setFlipped(false);
                        }
                    }
                }
            }
        }
    }
}

bool MainWindow::gameMoveBy(int change)
{
    return(!gameMode() && game().moveByPly(change));
}

void MainWindow::updateMenuRecent()
{
    QMenu* menu = qobject_cast<QMenu*>(sender());
    if (menu)
    {
        menu->clear();
        int n = std::min(m_recentFiles.count(), static_cast<int>(MainWindow::MaxRecentFiles));
        for(int i = 0; i < n; i++)
        {
            QAction* action = new QAction(menu);
            action->setVisible(true);
            action->setData(m_recentFiles[i]);
            action->setText(QString("&%1: %2").arg(i + 1).arg(m_recentFiles[i]));
            connect(action, SIGNAL(triggered()), SLOT(slotFileOpenRecent()));
            menu->addAction(action);
        }
    }
}

void MainWindow::updateMenuDatabases()
{
    QMenu* menu = qobject_cast<QMenu*>(sender());
    if (menu)
    {
        menu->clear();
        int n = 1;
        
        const auto dbs = m_registry->databases();
        for (auto dbi: dbs)
        {
            if (dbi->isValid() && !dbi->IsBook())
            {
                QAction* action = new QAction(menu);
                action->setVisible(true);
                action->setData(QVariant::fromValue(dbi));
                action->setText(QString("&%1: %2").arg(n++).arg(dbi->dbName()));
                if (dbi == m_currentDatabase)
                {
                    action->setCheckable(true);
                    action->setChecked(true);
                }
                else
                {
                    connect(action, SIGNAL(triggered()), SLOT(slotDatabaseChange()));
                }
                menu->addAction(action);
            }
        }
    }
}

void MainWindow::setFavoriteDatabase(QString fname)
{
    QUrl url = QUrl::fromUserInput(fname);
    if ((url.scheme() == "http") || (url.scheme() == "https") || (url.scheme() == "ftp") || (url.scheme() == "sftp"))
    {
        m_databaseList->setFileFavorite(fname, true, 0);
    }
    else
    {
        m_databaseList->setFileFavorite(url.toLocalFile(), true, 0);
    }
}

void MainWindow::slotGameMoveFirst()
{
    gameMoveBy(-999);
}

void MainWindow::slotGameMovePrevious()
{
    gameMoveBy(-1);
}

void MainWindow::slotGameMoveLast()
{
    gameMoveBy(999);
}

void MainWindow::slotGameMoveNextN()
{
    gameMoveBy(10);
}

void MainWindow::slotGameMovePreviousN()
{
    gameMoveBy(-10);
}

void MainWindow::openDatabase(QString fname)
{
    openDatabaseUrl(fname, false);
}

QString MainWindow::ficsPath() const
{
    return DatabaseInfo::ficsPath();
}

void MainWindow::openDatabaseUrl(QString fname, bool utf8)
{
    QFileInfo fi(fname);
    QUrl url = QUrl::fromUserInput(fname);
    if (!fi.completeSuffix().isEmpty())
    {
        if (url.isValid() && !url.isRelative() &&
            ((url.scheme() == "http") || (url.scheme() == "https") || (url.scheme() == "ftp") || (url.scheme() == "sftp")))
        {
            slotStatusMessage(tr("Start loading database..."));
            connect(downloadManager, SIGNAL(downloadError(QUrl)), this, SLOT(loadError(QUrl)), static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
            connect(downloadManager, SIGNAL(onDownloadFinished(QUrl, QString)), this, SLOT(loadReady(QUrl, QString)), static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
            downloadManager->doDownload(url);
        }
        else
        {
            openDatabaseArchive(url.toLocalFile(), utf8);
        }
    }
    else if (fname == "Clipboard")
    {
        ActivateDatabase("Clipboard");
    }
}

QString MainWindow::favoriteUrl() const
{
    QString url = AppSettings->getValue("Web/Favorite1").toString();
    int n = AppSettings->getValue("Web/AutoNumber1").toInt();
    url.replace("$1",QString::number(n));
    return url;
}

void MainWindow::openWebFavorite()
{
    QString url = favoriteUrl();
    if (!url.isEmpty())
    {
        openDatabaseUrl(favoriteUrl(), false);
    }
    else
    {
        slotConfigure(3); // TODO: Horrible hack
    }
}

void MainWindow::openFICS()
{
    openDatabaseFile(ficsPath(), false);
}

void MainWindow::openDatabaseArchive(QString fname, bool utf8)
{
    QFileInfo fi = QFileInfo(fname);
    QString ext = fi.suffix().toLower();
    if(fname.isEmpty() ||
            ext == "pgn" ||
            ext == "si4" ||
            ext == "ctg" ||
            ext == "bin" ||
            ext == "abk")
    {
        openDatabaseFile(fname, utf8);
    }
    else
    {
        QString dir = AppSettings->commonDataPath();

        fname = fi.canonicalFilePath();

        if(!fname.isEmpty())
        {
            QuaZip zip(fname);
            if(zip.open(QuaZip::mdUnzip))
            {
                // first, we need some information about archive itself
                // QString comment = zip.getComment();
                // and now we are going to access files inside it
                QuaZipFile file(&zip);
                for(bool more = zip.goToFirstFile(); more; more = zip.goToNextFile())
                {
                    file.open(QIODevice::ReadOnly);
                    QString outName = dir + QDir::separator() + file.getActualFileName();
                    QDir pathOut;
                    outName = pathOut.absoluteFilePath(outName);
                    if(!QFile::exists(outName))
                    {
                        QDir().mkpath(dir);

                        QFile out(outName);
                        if(out.open(QIODevice::WriteOnly))
                        {
                            out.write(file.readAll());
                            out.close();
                            openDatabaseFile(outName, utf8);
                        }
                        else
                        {
                            qDebug() << "File Error: " << out.error();
                        }
                    }
                    else
                    {
                        openDatabaseFile(outName, utf8);
                    }

                    file.close();
                }
                zip.close();
            }
        }
    }
}

bool MainWindow::ActivateFICSDatabase()
{
    return ActivateDatabase(ficsPath());
}

bool MainWindow::ActivateDatabase(QString fname)
{
    /* Check if the database is open */
    const auto dbs = m_registry->databases();
    for (auto dbi: dbs)
    {
        if (dbi->database()->filename() == fname)
        {
            if (dbi->isValid())
            {
                if (!dbi->IsBook())
                {
                    autoGroup->untrigger();
                    if (dbi != m_currentDatabase)
                    {
                        m_currentDatabase = dbi;
                        m_databaseList->setFileCurrent(fname);
                        slotDatabaseChanged();
                    }
                    activateBoardViewForDbIndex(m_currentDatabase);
                }
            }
            else
            {
                slotStatusMessage(tr("Database %1 cannot be accessed at the moment.").arg(fname));
            }
            return true;
        }
    }
    return false;
}

void MainWindow::openDatabaseFile(QString fname, bool utf8)
{
    QFileInfo fi = QFileInfo(fname);
    QString s = ficsPath();
    if (fname == s)
    {
        // Just touch FICS File to make sure it exists
        QFile f(s);
        f.open(QFile::ReadWrite);
        f.close();
    }

    fname = fi.canonicalFilePath();
    if (fname.isEmpty())
    {
        slotStatusMessage("File not found.");
        return;
    }
    if (ActivateDatabase(fname))
    {
        /* Check if the database is already open */
        return;
    }

    // Create database, connect progress bar and open file
    DatabaseInfo* db = new DatabaseInfo(&m_undoGroup,fname);
    QString basefile = fi.completeBaseName();

    startOperation(tr("Opening %1...").arg(basefile));
    connect(db->database(), SIGNAL(progress(int)), SLOT(slotOperationProgress(int)), Qt::QueuedConnection);
    connect(db, SIGNAL(LoadFinished(DatabaseInfo*)), this, SLOT(slotDataBaseLoaded(DatabaseInfo*)), Qt::QueuedConnection);
    connect(db, SIGNAL(signalRestoreState(GameX)), SLOT(slotDbRestoreState(GameX)));
    connect(db, SIGNAL(signalGameModified(bool)), SLOT(slotGameChanged(bool)));
    connect(db, SIGNAL(signalMoveChanged()), SLOT(slotMoveChanged()));
    connect(db, SIGNAL(signalGameModified(bool)), SIGNAL(signalGameModified(bool)));
    if(!db->open(utf8))
    {
        slotDataBaseLoaded(db);
    }
    else
    {
        m_registry->m_databases.append(db);
    }
}

void MainWindow::loadError(QUrl url)
{
    QFileInfo fi = QFileInfo(url.toString());
    slotStatusMessage(tr("Database %1 cannot be accessed at the moment (%2).").arg(fi.fileName(), url.errorString()));
}

void MainWindow::loadReady(QUrl url, QString fileName)
{
    QString fav = favoriteUrl();
    QString favurl = AppSettings->getValue("Web/Favorite1").toString();
    if ((url == fav) && favurl.contains("$1"))
    {
        int n = AppSettings->getValue("Web/AutoNumber1").toInt();
        ++n;
        AppSettings->setValue("Web/AutoNumber1",n);
    }
    openDatabaseArchive(fileName, false);
}

void MainWindow::slotDataBaseLoaded(DatabaseInfo* db)
{
    if(!db->IsLoaded())
    {
        cancelOperation(tr("Cannot open file"));
        m_registry->m_databases.removeOne(db);
        delete db;
        return;
    }
    QString fname = db->displayName();
    QFileInfo fi = QFileInfo(fname);
    QString basefile = fi.completeBaseName();

    m_databaseList->addFileOpen(fname, db->IsUtf8());

    finishOperation(tr("%1 opened").arg(basefile));

    if (!db->IsBook())
    {
        const auto dbs = m_registry->databases();
        for (auto dbi: dbs)
        {
            if (dbi->database()->filename() == fname)
            {
                if (!dbi->IsBook())
                {
                    autoGroup->untrigger();
                    m_currentDatabase = dbi;
                    CreateBoardView();
                }
                break;
            }
        }
        m_databaseList->setFileCurrent(fname);

        GameId lastGameIndex = m_databaseList->getLastIndex(fname);
        gameLoad(lastGameIndex);
        emit signalGameLoaded(game().startingBoard());

        slotDatabaseChanged();

        if (qobject_cast<FicsDatabase*>(db->database()))
        {
            if (!m_ficsClient->sessionStarted())
            {
                m_ficsClient->startSession();
            }
        }
    }

    m_recentFiles.append(fname);
    emit signalDatabaseOpenClose();
}

QString MainWindow::exportFileName(int& format)
{
    QFileDialog fd(this);
    fd.setAcceptMode(QFileDialog::AcceptSave);
    fd.setFileMode(QFileDialog::AnyFile);
    fd.setWindowTitle(tr("Export games"));
    fd.setViewMode(QFileDialog::Detail);
    fd.setDirectory(QDir::homePath());
    QStringList filters;
    filters << tr("PGN file (*.pgn)")
            << tr("HTML page (*.html)")
            << tr("LaTeX document (*.tex)");
#if QT_VERSION < 0x050000
    fd.setFilters(filters);
#else
    fd.setNameFilters(filters);
#endif
    if(fd.exec() != QDialog::Accepted)
    {
        return QString();
    }
#if QT_VERSION < 0x050000
    if(fd.selectedFilter().contains("*.tex"))
    {
        format = Output::Latex;
    }
    else if(fd.selectedFilter().contains("*.html"))
    {
        format = Output::Html;
    }
#else
    if(fd.selectedNameFilter().contains("*.tex"))
    {
        format = Output::Latex;
    }
    else if(fd.selectedNameFilter().contains("*.html"))
    {
        format = Output::Html;
    }
#endif
    else
    {
        format = Output::Pgn;
    }
    return fd.selectedFiles().constFirst();
}

bool MainWindow::gameEditComment(Output::CommentType type)
{
    QString annotation;
    int moves;
    int comments;
    int nags;
    game().moveCount(&moves, &comments, &nags);

    if((type == Output::Precomment) || (moves <= 0))
    {
        annotation = game().annotation(CURRENT_MOVE, GameX::BeforeMove);
    }
    else
    {
        annotation = game().annotation();
    }
    CommentDialog dlg(this);
    dlg.setText(annotation);
    if(!dlg.exec())
    {
        return false;
    }

    if((type == Output::Precomment) || (moves <= 0))
    {
        if(moves > 0)
        {
            QString spec = game().specAnnotations(CURRENT_MOVE, GameX::BeforeMove);
            game().setAnnotation(dlg.text()+spec, CURRENT_MOVE, GameX::BeforeMove);
        }
        else
        {
            QString spec = game().specAnnotations();
            game().setGameComment(dlg.text()+spec);
        }
    }
    else
    {
        QString spec = game().specAnnotations();
        if (moves > 0)
        {
            game().setAnnotation(dlg.text()+spec);
        }
        else
        {
            game().setGameComment(dlg.text()+spec);
        }
    }
    return true;
}

QAction* MainWindow::createAction(QObject* parent, QString name, const char* slot, const QKeySequence& key, QToolBar* pToolBar, QString image,
                                  const QString& tip, QAction::MenuRole menuRole)
{
    return createAction(name, slot, key, pToolBar, image, tip, menuRole, parent);
}

QAction* MainWindow::createAction(QString name, const char* slot, const QKeySequence& key, QToolBar* pToolBar, QString image,
                                  const QString& tip, QAction::MenuRole menuRole, QObject* parent)
{
    return createAction(name, slot, key, pToolBar, QIcon(image), tip, menuRole, parent);
}

QAction* MainWindow::createAction(QString name, const char* slot, const QKeySequence& key, QToolBar* pToolBar, QIcon icon,
                                  const QString& tip, QAction::MenuRole menuRole, QObject* parent)
{
    KbAction* action;
    if (!parent)
    {
        parent = this;
    }

    if(icon.isNull())
    {
        action = new KbAction(name, parent, key);
    }
    else
    {
        action = new KbAction(icon, name, parent, key);
    }

    if(!tip.isEmpty())
    {
        action->setStatusTip(tip);
    }

    if(slot)
    {
        connect(action, SIGNAL(triggered()), slot);
    }
    action->setMenuRole(menuRole);
    if(pToolBar)
    {
        pToolBar->addAction(action);
    }
    return action;
}

void MainWindow::setupActions()
{
    /* File menu */
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QToolBar* fileToolBar = addToolBar(tr("File"));
    fileToolBar->setObjectName("FileToolBar");
    file->addAction(createAction(tr("&New database..."), SLOT(slotFileNew()), Qt::CTRL + Qt::SHIFT + Qt::Key_N, fileToolBar, ":/images/new.png"));
    file->addAction(createAction(tr("&Open..."), SLOT(slotFileOpen()), QKeySequence::Open, fileToolBar, ":/images/folder_open.png"));
    file->addAction(createAction(tr("Open in UTF8..."), SLOT(slotFileOpenUtf8()), QKeySequence()));
    file->addAction(createAction(tr("Open FICS"), SLOT(openFICS()), QKeySequence(), fileToolBar, ":/images/fics.png"));
    file->addAction(createAction(tr("Web Favorite"), SLOT(openWebFavorite()), QKeySequence(), fileToolBar, ":/images/folder_web.png"));

    QMenu* menuRecent = file->addMenu(tr("Open recent"));
    connect(menuRecent, SIGNAL( aboutToShow()), this, SLOT(updateMenuRecent()));
    file->addSeparator();

    QAction* commitAction = createAction(tr("Save Database"), SLOT(slotFileSave()), Qt::CTRL + Qt::SHIFT + Qt::Key_S, fileToolBar, ":/images/save.png");
#ifndef QT_NO_TOOLTIP
    commitAction->setToolTip(tr("Commit Database to disk"));
#endif // QT_NO_TOOLTIP
    connect(this, SIGNAL(signalCurrentDBisReadWrite(bool)), commitAction, SLOT(setEnabled(bool)));
    file->addAction(commitAction);

    QMenu* exportMenu = file->addMenu(tr("&Export..."));
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), exportMenu, SLOT(setEnabled(bool)));
    exportMenu->addAction(createAction(exportMenu,tr("Current Game"), SLOT(slotFileExportGame())));
    exportMenu->addAction(createAction(exportMenu,tr("&Games in filter"), SLOT(slotFileExportFilter())));
    exportMenu->addAction(createAction(exportMenu,tr("&All games"), SLOT(slotFileExportAll())));

    QAction* saveImage = createAction(tr("Export Image..."), SLOT(slotExportImage()));
    file->addAction(saveImage);
    file->addAction(createAction(tr("Mail PGN..."), SLOT(slotSendMail()), QKeySequence(), fileToolBar, ":/images/edit_mail.png"));

    file->addSeparator();

    QAction* closeFileAction = createAction(tr("&Close"), SLOT(slotFileClose()), QKeySequence::Close, fileToolBar, ":/images/folder.png");
    connect(this, SIGNAL(signalCurrentDBcanBeClosed(bool)), closeFileAction, SLOT(setEnabled(bool)));
    file->addAction(closeFileAction);

    file->addAction(createAction(tr("&Quit"), SLOT(slotFileQuit()), QKeySequence(), nullptr, QString(), QString(), QAction::QuitRole));

    /* Edit menu */
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QToolBar* editToolBar = addToolBar(tr("Edit"));
    editToolBar->setObjectName("EditToolBar");

    QAction *undoAction = m_undoGroup.createUndoAction(this, tr("Undo"));
    QAction *redoAction = m_undoGroup.createRedoAction(this, tr("Redo"));
    undoAction->setShortcut(Qt::CTRL + Qt::Key_Z);
    redoAction->setShortcuts({Qt::CTRL + Qt::Key_Y, Qt::CTRL + Qt::SHIFT + Qt::Key_Z});
    undoAction->setIcon(QIcon(":/images/undo.png"));
    redoAction->setIcon(QIcon(":/images/redo.png"));

    editToolBar->addAction(undoAction);
    editToolBar->addAction(redoAction);
    editToolBar->addSeparator();

    edit->addAction(undoAction);
    edit->addAction(redoAction);
    edit->addSeparator();

    QAction* commentAfter = createAction(tr("Comment"), SLOT(slotEditComment()),
                                         Qt::CTRL + Qt::Key_A, editToolBar, ":/images/edit_after.png");
    connect(this, SIGNAL(signalGameIsEmpty(bool)), commentAfter, SLOT(setDisabled(bool)));
    edit->addAction(commentAfter);
    QAction* commentBefore = createAction(tr("Comment Before"), SLOT(slotEditCommentBefore()),
                                          Qt::CTRL + Qt::ALT + Qt::Key_A);
    connect(this, SIGNAL(signalGameAtLineStart(bool)), commentBefore, SLOT(setEnabled(bool)));
    edit->addAction(commentBefore);

    QMenu* editVariation = edit->addMenu(tr("Variation"));

    QAction* promoteAction = createAction(editVariation, tr("Promote"), SLOT(slotEditVarPromote()), Qt::CTRL + Qt::Key_J, editToolBar, ":/images/format_indent_less.png");
#ifndef QT_NO_TOOLTIP
    promoteAction->setToolTip(tr("Promote Variation"));
#endif // QT_NO_TOOLTIP
    connect(this, SIGNAL(signalMoveHasParent(bool)), promoteAction, SLOT(setEnabled(bool)));
    editVariation->addAction(promoteAction);

    QAction* removeVariationAction = createAction(editVariation, tr("Remove"), SLOT(slotEditVarRemove()),
                                     Qt::CTRL + Qt::Key_Delete, editToolBar, ":/images/edit_cut.png");
#ifndef QT_NO_TOOLTIP
    removeVariationAction->setToolTip(tr("Remove Variation"));
#endif // QT_NO_TOOLTIP
    connect(this, SIGNAL(signalMoveHasParent(bool)), removeVariationAction, SLOT(setEnabled(bool)));
    editVariation->addAction(removeVariationAction);

    QMenu* editremove = edit->addMenu(tr("&Remove"));
    editremove->addAction(createAction(editremove, tr("Moves from the beginning"),
                                       SLOT(slotEditTruncateStart()), QKeySequence()));
    editremove->addAction(createAction(editremove, tr("Moves to the end"), SLOT(slotEditTruncateEnd()),
                                       Qt::SHIFT + Qt::Key_Delete));
    edit->addSeparator();
    QAction* setupBoard = createAction(tr("Setup &position..."),
                                       SLOT(slotEditBoard()), Qt::SHIFT + Qt::CTRL + Qt::Key_E,
                                       editToolBar, ":/images/setup_board.png");

    edit->addAction(setupBoard);
    edit->addSeparator();
    edit->addAction(createAction(tr("Copy PGN"), SLOT(slotEditCopyPGN()), Qt::CTRL + Qt::Key_C, editToolBar, ":/images/edit_copy.png"));
    edit->addAction(createAction(tr("Copy FEN"), SLOT(slotEditCopyFEN()), Qt::CTRL + Qt::SHIFT + Qt::Key_C));
    edit->addAction(createAction(tr("Copy Position"), SLOT(slotEditCopyHumanFEN())));
    edit->addAction(createAction(tr("Copy Image"), SLOT(slotEditCopyImage()),
                                 Qt::CTRL + Qt::ALT + Qt::Key_C, editToolBar, ":/images/camera.png"));
    edit->addSeparator();
    edit->addAction(createAction(tr("Paste into new game"), SLOT(slotEditPaste()),
                                 Qt::CTRL + Qt::SHIFT + Qt::Key_V));
    edit->addAction(createAction(tr("Paste"), SLOT(slotEditMergePGN()),
                                 Qt::CTRL + Qt::Key_V, editToolBar, ":/images/edit_paste.png"));
    edit->addSeparator();
    brushGroup = new ExclusiveActionGroup(this);
    QMenu* brush = edit->addMenu(tr("Brush"));
    QAction* brushAction = createAction(tr("Green"), SLOT(slotToggleBrush()), Qt::META + Qt::Key_1, editToolBar, ":/images/brush_green.png");
    brushAction->setCheckable(true);
    brushAction->setData(QChar('G'));
    brush->addAction(brushAction);
    brushGroup->addAction(brushAction);
    brushAction = createAction(tr("Yellow"), SLOT(slotToggleBrush()), Qt::META + Qt::Key_2, editToolBar, ":/images/brush_yellow.png");
    brushAction->setCheckable(true);
    brushAction->setData(QChar('Y'));
    brush->addAction(brushAction);
    brushGroup->addAction(brushAction);
    brushAction = createAction(tr("Red"), SLOT(slotToggleBrush()), Qt::META + Qt::Key_3, editToolBar, ":/images/brush_red.png");
    brushAction->setCheckable(true);
    brushAction->setData(QChar('R'));
    brush->addAction(brushAction);
    brushGroup->addAction(brushAction);
    brushAction = createAction(tr("Erase"), SLOT(slotToggleBrush()), Qt::META + Qt::Key_4, editToolBar, ":/images/brush_erase.png");
    brushAction->setCheckable(true);
    brushAction->setData(QChar(0));
    brush->addAction(brushAction);
    brushGroup->addAction(brushAction);
    edit->addSeparator();

    edit->addAction(createAction(tr("&Preferences..."), SLOT(slotConfigure()), QKeySequence(), nullptr,
                                 QString(), QString(), QAction::PreferencesRole));

    /* View menu */
    m_menuView = menuBar()->addMenu(tr("&View"));

    QMenu* toolbars = m_menuView->addMenu(tr("Toolbars"));
    m_menuView->addSeparator();

#if defined(Q_OS_WINXX)
    QAction* stayOnTop = createAction(tr("Stay on Top"), SLOT(slotToggleStayOnTop()));
    stayOnTop->setCheckable(true);
    stayOnTop->setChecked(AppSettings->getValue("/MainWindow/StayOnTop").toBool());
    m_menuView->addAction(stayOnTop);
    m_menuView->addSeparator();
#else
    AppSettings->setValue("/MainWindow/StayOnTop", false);
#endif

    m_menuView->addAction(createAction(tr("Close current board"), SLOT(slotCloseTabWidget()), Qt::CTRL + Qt::SHIFT + Qt::Key_W,
                                       nullptr, style()->standardIcon(QStyle::SP_TitleBarCloseButton)));
    m_menuView->addSeparator();

    QToolBar* viewToolBar = addToolBar(tr("View"));
    viewToolBar->setObjectName("ViewToolBarMain");
    QAction* showTargets = createAction(tr("Show target fields"), SLOT(slotShowTargetFields()), 0,
                                       viewToolBar, QIcon(":/images/show_targets.png"));
    m_menuView->addAction(showTargets);
    showTargets->setCheckable(true);
    showTargets->setChecked(AppSettings->getValue("/Board/showTargets").toBool());

    QAction* showThreat = createAction(tr("Show threat"), SLOT(slotShowThreat()), 0,
                                       viewToolBar, QIcon(":/images/show_threat.png"));
    m_menuView->addAction(showThreat);
    showThreat->setCheckable(true);
    showThreat->setChecked(AppSettings->getValue("/Board/showThreat").toBool());

    QAction* showAttackW = createAction(tr("Show covered squares from White"), SLOT(slotShowWhiteAttacks()), 0,
                                       viewToolBar, QIcon(":/images/white_wall.png"));
    m_menuView->addAction(showAttackW);
    showAttackW->setCheckable(true);
    showAttackW->setChecked(false);

    QAction* showAttackB = createAction(tr("Show covered squares from Black"), SLOT(slotShowBlackAttacks()), 0,
                                       viewToolBar, QIcon(":/images/black_wall.png"));
    m_menuView->addAction(showAttackB);
    showAttackB->setCheckable(true);
    showAttackB->setChecked(false);

    attackGroup = new ExclusiveActionGroup(this);
    attackGroup->addAction(showAttackW);
    attackGroup->addAction(showAttackB);

    QAction* showUnderprotectW = createAction(tr("Show underprotected white pieces"), SLOT(slotShowUnderprotectedWhite()), 0,
                                       viewToolBar, QIcon(":/images/threat_white.png"));
    m_menuView->addAction(showUnderprotectW);
    showUnderprotectW->setCheckable(true);
    showUnderprotectW->setChecked(false);

    QAction* showUnderprotectB = createAction(tr("Show underprotected black pieces"), SLOT(slotShowUnderprotectedBlack()), 0,
                                       viewToolBar, QIcon(":/images/threat_black.png"));
    m_menuView->addAction(showUnderprotectB);
    showUnderprotectB->setCheckable(true);
    showUnderprotectB->setChecked(false);

    underprotectGroup = new ExclusiveActionGroup(this);
    underprotectGroup->addAction(showUnderprotectW);
    underprotectGroup->addAction(showUnderprotectB);

    m_menuView->addSeparator();

    /* Game menu */
    QMenu *gameMenu = menuBar()->addMenu(tr("&Game"));
    QToolBar* gameToolBar = addToolBar(tr("Game"));
    gameToolBar->setObjectName("GameToolBarMain");
    QToolBar* dbToolBar = addToolBar(tr("Database"));
    dbToolBar->setObjectName("DbToolBarMain");

    QAction* newAction = createAction(tr("&New"), SLOT(slotGameNew()), QKeySequence::New,
                                      dbToolBar, ":/images/new_game.png");
    gameMenu->addAction(newAction);
    connect(this, SIGNAL(signalGameIsEmpty(bool)), newAction, SLOT(setDisabled(bool)));

    QMenu* loadMenu = gameMenu->addMenu(tr("&Load"));
    m_recentGames = gameMenu->addMenu(tr("Load recent"));

    /* Game->Load submenu */
    QAction * prevAction = createAction(tr("&Previous"), SLOT(slotGameLoadPrevious()), Qt::Key_F3,
                                        dbToolBar, ":/images/game_up.png");
    QAction * nextAction = createAction(tr("&Next"), SLOT(slotGameLoadNext()), Qt::Key_F4,
                                        dbToolBar, ":/images/game_down.png");
    QAction * goAction = createAction(tr("&Go to game..."), SLOT(slotGameLoadChosen()), Qt::CTRL + Qt::Key_G);
    QAction * rndAction = createAction(tr("&Random"), SLOT(slotGameLoadRandom()), Qt::CTRL + Qt::Key_Question,
                                     dbToolBar, ":/images/rnd_game.png");
    connect(this, SIGNAL(signalFilterEmpty(bool)), goAction, SLOT(setDisabled(bool)));
    connect(this, SIGNAL(signalFilterEmpty(bool)), rndAction, SLOT(setDisabled(bool)));
    connect(m_gameList, SIGNAL(signalLastGameLoaded(bool)), nextAction, SLOT(setDisabled(bool)));
    connect(m_gameList, SIGNAL(signalFirstGameLoaded(bool)), prevAction, SLOT(setDisabled(bool)));
    connect(m_gameList, SIGNAL(signalDropEvent(QDropEvent*)), this, SLOT(slotDatabaseDropped(QDropEvent*)));
    connect(m_gameList, SIGNAL(signalFilterSize(quint64)), this, SLOT(slotFilterSizeChanged(quint64)));
    loadMenu->addAction(nextAction);
    loadMenu->addAction(prevAction);
    loadMenu->addAction(goAction);
    loadMenu->addAction(rndAction);

    QAction* saveAction = createAction(tr("&Save..."), SLOT(slotGameSave()), QKeySequence::Save);
    gameMenu->addAction(saveAction);
    QAction* saveOnlyAction = createAction(tr("Save"), SLOT(slotGameSaveOnly()));
    connect(this, SIGNAL(signalGameModified(bool)), saveOnlyAction, SLOT(setEnabled(bool)));
    gameMenu->addAction(saveOnlyAction);

    gameMenu->addAction(createAction(tr("Edit tags..."), SLOT(slotGameEditTags())));

    gameMenu->addSeparator();

    QAction* flip = createAction(tr("&Flip board"), SLOT(slotConfigureFlip()), Qt::CTRL + Qt::Key_B, gameToolBar, ":/images/flip_board.png");
    flip->setCheckable(true);
    connect(m_ficsConsole, SIGNAL(SignalPlayerIsBlack(bool,bool)), flip, SLOT(setChecked(bool)));
    gameToolBar->addSeparator();

    autoGroup = new ExclusiveActionGroup(this);

    m_match = createAction(tr("Match"), SLOT(slotToggleGameMode()), Qt::CTRL + Qt::Key_M, gameToolBar, ":/images/black_chess.png");
    m_match->setCheckable(true);

    autoGroup->addAction(m_match);
    gameMenu->addAction(m_match);
    gameMenu->addSeparator();
    connect(m_match, SIGNAL(changed()), SLOT(slotToggleGameMode()));

#ifdef USE_SPEECH
    gameMenu->addAction(createAction(tr("Read moves ahead"), SLOT(slotReadAhead()), Qt::CTRL + Qt::Key_Period, gameToolBar, ":/images/readAhead.png"));
#endif

    m_training = createAction(tr("Training"), SLOT(slotToggleTraining()), Qt::CTRL + Qt::Key_R, gameToolBar, ":/images/training.png");
    m_training->setCheckable(true);
    autoGroup->addAction(m_training);
    gameMenu->addAction(m_training);

    m_training2 = createAction(tr("Train both sides"), SLOT(slotToggleTraining2()), Qt::CTRL + Qt::META + Qt::Key_R, gameToolBar, ":/images/training_both.png");
    m_training2->setCheckable(true);
    autoGroup->addAction(m_training2);
    gameMenu->addAction(m_training2);

    m_autoPlay = createAction(tr("Auto Player"), SLOT(slotToggleAutoPlayer()), Qt::CTRL + Qt::SHIFT + Qt::Key_R, gameToolBar, ":/images/replay.png");
    gameMenu->addAction(m_autoPlay);
    autoGroup->addAction(m_autoPlay);
    m_autoPlay->setCheckable(true);

    m_autoAnalysis = createAction(tr("Auto Analysis"), SLOT(slotToggleAutoAnalysis()), Qt::CTRL + Qt::ALT + Qt::Key_R, gameToolBar, ":/images/annotate.png");
    gameMenu->addAction(m_autoAnalysis);
    autoGroup->addAction(m_autoAnalysis);
    m_autoAnalysis->setCheckable(true);

    m_autoGame = createAction(tr("Play engine"), SLOT(slotToggleGamePlayer()), Qt::CTRL + Qt::SHIFT + Qt::Key_G, gameToolBar, ":/images/respond.png");
    gameMenu->addAction(m_autoGame);
    autoGroup->addAction(m_autoGame);
    m_autoGame->setCheckable(true);

    m_autoRespond = createAction(tr("Match against engine"), SLOT(slotToggleAutoRespond()), Qt::META + Qt::SHIFT + Qt::Key_G, gameToolBar, ":/images/game_engine.png");
    gameMenu->addAction(m_autoRespond);
    autoGroup->addAction(m_autoRespond);
    m_autoRespond->setCheckable(true);

    m_engineMatch = createAction(tr("Engine Match"), SLOT(slotToggleEngineMatch()), Qt::CTRL + Qt::ALT + Qt::Key_M, gameToolBar, ":/images/chip.png");
    gameMenu->addAction(m_engineMatch);
    autoGroup->addAction(m_engineMatch);
    m_engineMatch->setCheckable(true);

    gameToolBar->addSeparator();
    gameMenu->addSeparator();

    gameMenu->addAction(flip);

    /* Game->Go to submenu */
    QMenu* goMenu = gameMenu->addMenu(tr("&Go to"));
    QAction* gotoFirstMove = createAction(goMenu, tr("&Start"), SLOT(slotGameMoveFirst()), Qt::Key_Home, gameToolBar, ":/images/first.png");
#ifndef QT_NO_TOOLTIP
    gotoFirstMove->setToolTip(tr("Go to first move"));
#endif // QT_NO_TOOLTIP
    connect(this, SIGNAL(signalMoveHasPreviousMove(bool)), gotoFirstMove, SLOT(setEnabled(bool)));
    goMenu->addAction(gotoFirstMove);

    QAction* actionPrevMove = createAction(goMenu, tr("&Previous move"), SLOT(slotGameMovePrevious()), Qt::Key_Left, gameToolBar, ":/images/prev.png");
    connect(this, SIGNAL(signalMoveHasPreviousMove(bool)), actionPrevMove, SLOT(setEnabled(bool)));
    goMenu->addAction(actionPrevMove);

    QAction* actionNextMove = createAction(goMenu, tr("&Next move"), SLOT(slotGameMoveNext()), Qt::Key_Right, gameToolBar, ":/images/next.png");
    connect(this, SIGNAL(signalMoveHasNextMove(bool)), actionNextMove, SLOT(setEnabled(bool)));
    goMenu->addAction(actionNextMove);

    QAction* gotoLastMove = createAction(goMenu, tr("&End"), SLOT(slotGameMoveLast()), Qt::Key_End, gameToolBar, ":/images/last.png");
#ifndef QT_NO_TOOLTIP
    gotoLastMove->setToolTip(tr("Go to last move"));
#endif // QT_NO_TOOLTIP
    connect(this, SIGNAL(signalMoveHasNextMove(bool)), gotoLastMove, SLOT(setEnabled(bool)));
    goMenu->addAction(gotoLastMove);

    QAction* fiveMovesForward = createAction(goMenu, tr("5 moves &forward"), SLOT(slotGameMoveNextN()), Qt::Key_Down);
    connect(this, SIGNAL(signalMoveHasNextMove(bool)), fiveMovesForward, SLOT(setEnabled(bool)));
    goMenu->addAction(fiveMovesForward);

    QAction* fiveMovesPrev = createAction(goMenu, tr("5 moves &backward"), SLOT(slotGameMovePreviousN()), Qt::Key_Up);
    connect(this, SIGNAL(signalMoveHasPreviousMove(bool)), fiveMovesPrev, SLOT(setEnabled(bool)));
    goMenu->addAction(fiveMovesPrev);

    QAction* enterVariation = createAction(goMenu, tr("Enter Variation"), SLOT(slotGameVarEnter()), Qt::CTRL + Qt::Key_Right, gameToolBar, ":/images/go_in.png");
    connect(this, SIGNAL(signalMoveHasVariation(bool)), enterVariation, SLOT(setEnabled(bool)));
    goMenu->addAction(enterVariation);

    QAction* prevVariation = createAction(goMenu, tr("Previous Variation"), SLOT(slotGameVarUp()), Qt::CTRL + Qt::Key_Up, gameToolBar, ":/images/go_up.png");
    connect(this, SIGNAL(signalVariationHasSibling(bool)), prevVariation, SLOT(setEnabled(bool)));
    goMenu->addAction(prevVariation);

    QAction* nextVariation = createAction(goMenu, tr("Next Variation"), SLOT(slotGameVarDown()), Qt::CTRL + Qt::Key_Down, gameToolBar, ":/images/go_down.png");
    connect(this, SIGNAL(signalVariationHasSibling(bool)), nextVariation, SLOT(setEnabled(bool)));
    goMenu->addAction(nextVariation);

    QAction* backToMainLine = createAction(goMenu, tr("Back to main line"), SLOT(slotGameVarExit()), Qt::CTRL + Qt::Key_Left, gameToolBar, ":/images/go_out.png");
    connect(this, SIGNAL(signalMoveHasParent(bool)), backToMainLine, SLOT(setEnabled(bool)));
    goMenu->addAction(backToMainLine);

    gameMenu->addSeparator();

    QMenu* refactorMenu = gameMenu->addMenu(tr("Refactor"));
    refactorMenu->addAction(createAction(refactorMenu, tr("Uncomment"), SLOT(slotGameUncomment())));
    refactorMenu->addAction(createAction(refactorMenu, tr("Remove Time"), SLOT(slotGameRemoveTime())));
    refactorMenu->addAction(createAction(refactorMenu, tr("Remove Variations"), SLOT(slotGameRemoveVariations())));

    /* Search menu */
    QMenu* search = menuBar()->addMenu(tr("Fi&nd"));
    QToolBar* searchToolBar = addToolBar(tr("Search"));
    searchToolBar->setObjectName("SearchToolBar");

    QAction* actionFindTag = createAction(tr("Find tag..."), SLOT(slotSearchTag()), Qt::CTRL + Qt::SHIFT + Qt::Key_T, searchToolBar, ":/images/find_tag.png");
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), actionFindTag, SLOT(setEnabled(bool)));
    search->addAction(actionFindTag);

    QAction* actionFindBoard = createAction(tr("Find position..."), SLOT(slotSearchBoard()), Qt::CTRL + Qt::SHIFT + Qt::Key_B, searchToolBar, ":/images/find_pos.png");
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), actionFindBoard, SLOT(setEnabled(bool)));
    search->addAction(actionFindBoard);

    search->addSeparator();

    QAction* duplicates = createAction(tr("Filter duplicate games"), SLOT(slotDatabaseFilterDuplicateGames()));
    search->addAction(duplicates);
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), duplicates, SLOT(setEnabled(bool)));

    duplicates = createAction(tr("Filter duplicate headers"), SLOT(slotDatabaseFilterDuplicateTags()));
    search->addAction(duplicates);
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), duplicates, SLOT(setEnabled(bool)));

    search->addSeparator();

    QAction* filterReset = createAction(tr("Reset filter"), SLOT(slotSearchReset()), Qt::CTRL + Qt::Key_F, searchToolBar, ":/images/filter_reset.png");
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), filterReset, SLOT(setEnabled(bool)));
    search->addAction(filterReset);

    QAction* reverseFilter = createAction(tr("Reverse filter"), SLOT(slotSearchReverse()), Qt::CTRL + Qt::SHIFT + Qt::Key_F, searchToolBar, ":/images/filter_rev.png");
    connect(this, SIGNAL(signalCurrentDBhasGames(bool)), reverseFilter, SLOT(setEnabled(bool)));
    search->addAction(reverseFilter);

    /* Database menu */
    QMenu* menuDatabase = menuBar()->addMenu(tr("&Database"));
    m_menuDatabases = menuDatabase->addMenu(tr("&Switch to"));
    connect(m_menuDatabases, SIGNAL( aboutToShow()), this, SLOT(updateMenuDatabases()));
    menuDatabase->addAction(createAction(tr("&Copy games..."), SLOT(slotDatabaseCopy()), Qt::Key_F5));
    QMenu* refactorMenu2 = menuDatabase->addMenu(tr("Refactor"));
    refactorMenu2->addAction(createAction(refactorMenu2, tr("Uncomment"), SLOT(slotDatabaseUncomment())));
    refactorMenu2->addAction(createAction(refactorMenu2, tr("Remove Time"), SLOT(slotDatabaseRemoveTime())));
    refactorMenu2->addAction(createAction(refactorMenu2, tr("Remove Variations"), SLOT(slotDatabaseRemoveVariations())));
    menuDatabase->addSeparator();
    menuDatabase->addAction(createAction(tr("Clear clipboard"), SLOT(slotDatabaseClearClipboard())));

    /* Scratchpad */
    QMenu* menuScratchPad = menuBar()->addMenu(tr("Scratch Pad"));
    /* Text Editor */
    DockWidgetEx* textEditDock = new DockWidgetEx(tr("Scratch Pad"), this);
    textEditDock->setObjectName("ScratchpadDock");
    m_scratchPad = new TextEdit(textEditDock,menuScratchPad);
    textEditDock->setWidget(m_scratchPad);
    // addDockWidget(Qt::RightDockWidgetArea, textEditDock);
    m_menuView->addAction(textEditDock->toggleViewAction());
    textEditDock->hide();
    connect(m_scratchPad, SIGNAL(requestBoardImage(QImage&,double)), this, SLOT(slotCreateBoardImage(QImage&,double)));
    connect(m_scratchPad, SIGNAL(linkActivated(QString)), this, SLOT(openDatabase(QString)));
    connect(this, SIGNAL(reconfigure()), m_scratchPad, SLOT(slotReconfigure()));

    /* Help menu */
    menuBar()->addSeparator();
    QMenu *help = menuBar()->addMenu(tr("&Help"));

    /* Help Window */
    DockWidgetEx* pHelpDock = new DockWidgetEx(tr("Help"), this);
    pHelpDock->setObjectName("Help");
    HelpBrowserShell* pHelpBrowser = new HelpBrowserShell(this);
    pHelpDock->setWidget(pHelpBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, pHelpDock);
    QAction* helpAction = pHelpDock->toggleViewAction();
    helpAction->setIcon(QIcon(":/images/help.png"));
    helpAction->setShortcut(Qt::Key_F1);
    help->addAction(helpAction);

    help->addAction(createAction(tr("Customize Keyboard..."), SLOT(slotEditActions())));
    help->addSeparator();
    help->addAction(createAction(tr("Load Sample Database"), SLOT(QueryLoadDatabase())));
    QAction* reportBugAction = createAction(tr("Report a bug..."), SLOT(slotHelpBug()));
    reportBugAction->setIcon(QIcon(":/images/bug.png"));
    help->addAction(reportBugAction);
    help->addSeparator();
    help->addAction(createAction(tr("&About ChessX"), SLOT(slotHelpAbout()), QString(), nullptr, QString(), QString(), QAction::AboutRole));

#ifdef QT_DEBUG
    QMenu* debug = help->addMenu("Debug");
    debug->addAction(createAction("Copy HTML", SLOT(slotGameViewSource())));
    debug->addAction(createAction("Dump Movenodes", SLOT(slotGameDumpMoveNodes())));
    debug->addAction(createAction("Dump Board", SLOT(slotGameDumpBoard())));
    debug->addAction(createAction("Make Screenshot", SLOT(slotScreenShot()), Qt::CTRL + Qt::Key_F12));
    debug->addAction(createAction("Compile ECO", SLOT(slotCompileECO())));
#endif

    fileToolBar->addAction(helpAction);
    toolbars->addAction(fileToolBar->toggleViewAction());
    toolbars->addAction(editToolBar->toggleViewAction());
    toolbars->addAction(dbToolBar->toggleViewAction());
    toolbars->addAction(gameToolBar->toggleViewAction());
    toolbars->addAction(searchToolBar->toggleViewAction());
    toolbars->addAction(viewToolBar->toggleViewAction());

    KbAction::restoreKeyboardLayoutForObject(this);
}

void MainWindow::slotFilterSizeChanged(quint64 newSize)
{
    emit signalFilterEmpty(newSize <= 1); // Not strictly empty, but there is no selection to be changed
}

bool MainWindow::confirmQuit()
{
    QString modified;

    if (!m_scratchPad->saveDocument())
        return false;

    const auto dbs = m_registry->databases();
    for (auto dbi: dbs)
    {
        if (!QuerySaveGame(dbi))
        {
            return false;
        }
    }
    for (auto dbi: dbs)
    {
        if (dbi->isValid() && dbi->database()->isModified() && !dbi->isClipboard())
        {
            modified += dbi->database()->name() + '\n';
        }
    }
    if(!modified.isEmpty())
    {
        int response = MessageDialog::yesNoCancel(tr("Following databases are modified:")
                       + '\n' + modified + tr("Save them?"));
        if(response == MessageDialog::Cancel)
        {
            return false;
        }
        if(response == MessageDialog::Yes)
        {
            Output output(Output::Pgn, &BoardView::renderImageForBoard);
            for (auto dbi: dbs)
            {
                if (dbi->database()->isModified())
                {
                    output.output(dbi->database()->filename(), *(dbi->database()));
                }
            }
        }
    }

    SwitchToClipboard();
    cancelPolyglotWriters();
    m_openingTreeWidget->cancel(); // Make sure we are not grabbing into something that is closed now

    for (int i = dbs.size() - 1; i; --i)
    {
        slotFileCloseIndex(i, true);
    }

    m_openingTreeWidget->cancel(); // Make sure that Clipboard tree update is terminated again

    return true;
}

void MainWindow::SwitchToClipboard()
{
    if (!m_currentDatabase || !m_currentDatabase->isClipboard())
    {
        m_currentDatabase = m_registry->databases().at(0); // Switch to clipboard is always safe
        activateBoardViewForDbIndex(databaseInfo());
        m_databaseList->setFileCurrent("Clipboard");
        slotDatabaseChanged();
    }
}

void MainWindow::startOperation(const QString& msg)
{
    ++m_operationFlag;
    slotStatusMessage(msg);
    if (m_operationFlag==1)
    {
        m_operationTime.start();
        m_progressBar->setMaximumHeight(m_statusFilter->height() - 3);
        statusBar()->insertPermanentWidget(0, m_progressBar);
        m_progressBar->setValue(0);
        m_progressBar->show();
    }
}

void MainWindow::finishOperation(const QString& msg)
{
    if (m_operationFlag)
    {
        --m_operationFlag;
        if (m_operationFlag == 0)
        {
            slotStatusMessage(msg + tr(" (%1 s.)").arg(m_operationTime.elapsed() / 100 / 10.0));
            statusBar()->removeWidget(m_progressBar);
        }
        else
        {
            slotStatusMessage(msg);
        }
    }
}

void MainWindow::cancelOperation(const QString& msg)
{
    if (m_operationFlag)
    {
        --m_operationFlag;
        slotStatusMessage(msg);
        if (m_operationFlag == 0)
        {
            statusBar()->removeWidget(m_progressBar);
        }
    }
}

bool MainWindow::QuerySaveGame(DatabaseInfo *dbInfo)
{
    bool shouldNotify = false;
    if (!dbInfo)
    {
        shouldNotify = true;
        dbInfo = databaseInfo();
    }
    if(dbInfo->gameNeedsSaving())
    {
        SaveDialog dlg(this);
        int n = dlg.save(dbInfo->database(), dbInfo->currentGame());
        if(n == QDialog::Accepted)
        {
            saveGame(dbInfo);
        }
        else if(n == SaveDialog::Discard)
        {
            dbInfo->setModified(false, GameX(), ""); // Do not notify more than once
        }

        if (shouldNotify)
        {
            emit databaseModified();
        }

        return (n != QDialog::Rejected);
    }
    return true;
}

void MainWindow::SimpleSaveGame()
{
    DatabaseInfo *dbInfo = databaseInfo();
    if(dbInfo && dbInfo->gameNeedsSaving())
    {
        saveGame(dbInfo);
        emit databaseModified();
    }
}

void MainWindow::restoreRecentFiles()
{
    AppSettings->beginGroup("Favorites");
    QStringList list = AppSettings->value("Files").toStringList();
    QStringList attributes = AppSettings->value("Attributes").toStringList();
    QList<int> indexList;
    AppSettings->list("LastGameIndex", indexList);
    AppSettings->endGroup();

    QStringList::const_iterator it = attributes.cbegin();
    QList<int>::const_iterator it1 = indexList.cbegin();
    foreach(QString s, list)
    {
        QString attribute = it != attributes.cend() ? *it++ : "";
        m_databaseList->setFileFavorite(s, true, *it1++);
        bool bUtf8 = (attribute.contains("utf8"));
        m_databaseList->setFileUtf8(s, bUtf8);
        QRegExp regExp("stars([0-9])");
        if (regExp.indexIn(attribute) >= 0)
        {
           QString d = regExp.cap(1);
           m_databaseList->setStars(s, d.toInt());
        }
    }
}

void MainWindow::loadFileFavorites()
{
    AppSettings->beginGroup("Favorites");
    QStringList list = AppSettings->value("Files").toStringList();
    QStringList attributes = AppSettings->value("Attributes").toStringList();
    AppSettings->endGroup();

    for (int stars=4;stars<6;++stars)
    {
        QStringList::const_iterator it = attributes.cbegin();
        foreach(QString s, list)
        {
            QString attribute = it != attributes.cend() ? *it++ : "";
            bool bUtf8 = (attribute.contains("utf8"));
            QRegExp regExp("stars([0-9])");
            if (regExp.indexIn(attribute) >= 0)
            {
               QString d = regExp.cap(1);
               if (d.toInt() == stars)
               {
                   openDatabaseFile(s, bUtf8);
               }
            }
        }
    }
}

void MainWindow::StartCheckUpdate()
{
    if(AppSettings->getValue("/General/onlineVersionCheck").toBool())
    {
        m_manager = new QNetworkAccessManager(this);
        connect(m_manager, SIGNAL(finished(QNetworkReply*)),
                SLOT(slotHttpDone(QNetworkReply*)));
        connect(this, SIGNAL(signalVersionFound(int, int, int)),
                SLOT(slotVersionFound(int, int, int)));
        QUrl url = QUrl(QString("http://chessx.sourceforge.net/versions/current.txt"));
        QNetworkRequest request(url);
        QByteArray userAgent = QString(QCoreApplication::applicationName() + "/" + STR_VERSION_NET).toLatin1();
        request.setRawHeader("User-Agent",userAgent);
        m_manager->get(request);
    }
}

void MainWindow::slotHttpDone(QNetworkReply *reply)
{
    QUrl url = reply->url();

    if (url.toString().endsWith("current.txt"))
    {
        if(!reply->error())
        {
            QString answer(reply->readAll());
            QRegExp rx("(\\d\\d?)\\.(\\d\\d?)\\.(\\d\\d?)");
            if(answer.indexOf(rx) > -1)
            {
                int major = rx.capturedTexts().at(1).toInt();
                int minor = rx.capturedTexts().at(2).toInt();
                int build = rx.capturedTexts().at(3).toInt();
                emit signalVersionFound(major, minor, build);
            }
        }
        reply->deleteLater();
        StartCheckDatabase();
    }
}

void MainWindow::QueryLoadDatabase()
{
    LoadQuery dlg(this);
    if(dlg.exec() == QDialog::Accepted)
    {
        if(dlg.largeDB())
        {
            openDatabaseUrl("http://chessx.sourceforge.net/db/bundesliga2000.pgn.zip", false);
        }
        else
        {
            openDatabaseUrl("http://chessx.sourceforge.net/db/SBL1213.pgn.zip", false);
        }
    }
    AppSettings->setValue("/General/BuiltinDbInstalled", QVariant(true));
}

void MainWindow::StartCheckDatabase()
{
    if ((m_recentFiles.count() == 0) && !AppSettings->getValue("/General/BuiltinDbInstalled").toBool())
    {
        QueryLoadDatabase();
    }
}

void MainWindow::slotVersionFound(int major, int minor, int build)
{
    int verInternet = major * 10000 + minor * 100 + build;
    int verCurrent = VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + REVISION;
    if(verInternet > verCurrent)
    {
        slotStatusMessage(tr("A new version is available at chessx.sourceforge.net"));
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    m_pDragTabBar = nullptr;
    m_tabDragIndex = -1;
    const QMimeData *mimeData = event->mimeData();
    const GameMimeData* gameMimeData = qobject_cast<const GameMimeData*>(mimeData);
    const DbMimeData* dbMimeData = qobject_cast<const DbMimeData*>(mimeData);
    if((mimeData && mimeData->hasUrls()) || gameMimeData || dbMimeData)
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    QTabBar* pTabBar = qobject_cast<QTabBar*>(childAt(event->pos()));

    if (pTabBar != nullptr && pTabBar->parent() == this)
    {
        QPoint globalPos = mapToGlobal(event->pos());
        QPoint widgetPos = pTabBar->mapFromGlobal(globalPos);

        int tabIndex = pTabBar->tabAt(widgetPos);
        if (tabIndex != m_tabDragIndex && pTabBar != m_pDragTabBar)
        {
            m_dragTimer->stop();
            m_tabDragIndex = tabIndex;
            m_pDragTabBar = pTabBar;
            m_dragTimer->start(200);
        }
        event->ignore();
        // TODO - figure out, if this tab really accepts the dragged type!
        return;
    }
    else if ((pTabBar != nullptr) && (pTabBar->parent() == m_tabWidget))
    {
        QPoint globalPos = mapToGlobal(event->pos());
        QPoint widgetPos = pTabBar->mapFromGlobal(globalPos);

        int tabIndex = pTabBar->tabAt(widgetPos);
        m_pDragTabBar = pTabBar;
        m_tabDragIndex = tabIndex;
        m_dragTimer->stop();
        m_dragTimer->start(200);
    }

    event->ignore();
    return;
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    m_pDragTabBar = nullptr;
    m_tabDragIndex = -1;
    m_dragTimer->stop();
    event->accept();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    m_pDragTabBar = nullptr;
    m_tabDragIndex = -1;
    m_dragTimer->stop();
    event->ignore();
}

void MainWindow::slotAutoSwitchTab()
{
    if (m_pDragTabBar && m_tabDragIndex != -1)
    {
        if (m_pDragTabBar->parent() != m_tabWidget)
        {
            m_pDragTabBar->setCurrentIndex(m_tabDragIndex);
        }
        else
        {
            slotActivateBoardView(m_tabDragIndex);
        }

        m_dragTimer->stop();
        m_pDragTabBar = nullptr;
        m_tabDragIndex = -1;
    }
}

bool MainWindow::gameMode() const
{
    return m_gameMode;
}

void MainWindow::setGameMode(bool gameMode)
{
    m_gameMode = gameMode;

    if (gameMode) brushGroup->untrigger();

    emit signalGameModeChanged(m_gameMode);

    emit signalMoveHasNextMove(!gameMode && !game().atLineEnd());
    emit signalMoveHasPreviousMove(!gameMode && !game().atGameStart());
    emit signalMoveHasVariation(!gameMode && game().variationCount() > 0);
    emit signalMoveHasParent(!gameMode && !game().isMainline());
    emit signalVariationHasSibling(!gameMode && game().variationHasSiblings(CURRENT_MOVE));
    emit signalGameAtLineStart(!gameMode && game().atLineStart());
}

QString MainWindow::PieceToSpeech(PieceType pt)
{
    QStringList pn;
    pn << tr("None") << tr("King") << tr("Queen")
       << tr("Rook") << tr("Bishop") << tr("Knight") << tr("Pawn");
    return pn.at(pt);
}

QString MainWindow::MoveToSpeech(Move m)
{
    if (m.isNullMove())
    {
        return tr("idea");
    }
    if (m.isCastling())
    {
        return m.isCastlingShort() ? tr("castles short") : tr("castles long");
    }
    QString s = PieceToSpeech(pieceType(m.pieceMoved()));
    s += " ";
    s += m.fromSquareString();
    s += " ";
    s += tr("to");
    s += " ";
    s += m.toSquareString();
    s += " ";
    if (m.isPromotion())
    {
        s += " ";
        s += tr("promotes to");
        s += " ";
        s = PieceToSpeech(pieceType(m.promotedPiece()));
    }
    return s;
}

bool MainWindow::announceMove(Move m)
{
#ifdef USE_SPEECH
    if (AppSettings->getValue("/Sound/Move").toInt() == 2)
    {
        QString s = MoveToSpeech(m);
        if (!s.isEmpty())
        {
            speech->say(s);
            return true;
        }
    }
#endif
    return false;
}

void MainWindow::playSound(QString s)
{
#ifdef USE_SOUND
    if (AppSettings->getValue("/Sound/Move").toInt()==1)
    {
        QSound::play(s);
    }
#endif
}
