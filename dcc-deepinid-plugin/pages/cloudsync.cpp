#include "cloudsync.h"
#include <DLabel>
#include <DTipLabel>
#include <widgets/switchwidget.h>
#include <DFontSizeManager>
#include <DFrame>
#include <DPalette>
#include <DListView>
#include <QDateTime>
#include <DToolButton>
#include <DHorizontalLine>
#include <DDialog>
#include <DToolTip>
#include <QEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <DGuiApplicationHelper>
#include <QPushButton>
#include <DSwitchButton>
#include "utils.h"
#include "trans_string.h"
#include "userdialog.h"

DWIDGET_USE_NAMESPACE
using namespace DCC_NAMESPACE;

Q_DECLARE_METATYPE(QMargins)

CloudSyncPage::CloudSyncPage(QWidget *parent):QWidget (parent)
  , m_autoSyncTips(new DTipLabel(TransString::getTransString(STRING_CLOUDMSG), this))
  , m_bindedTips(new DTipLabel(TransString::getTransString(STRING_CLOUDTITLE), this))
  , m_syncTimeTips(new DTipLabel("", this))
  , m_autoSyncSwitch(new SwitchWidget(this))
  , m_syncConfigView(new DListView(this))
  , m_syncItemView(new DListView(this))
  , m_labelClear(new DLabel(this))
  , m_configModel(new SyncConfigModel(this))
  , m_itemModel(new QStandardItemModel(this))
  , m_clearDlg(new DDialog("", TransString::getTransString(STRING_EMPTYMSG), this))
  , m_syncState(false)
{
    m_sysConfig =
    {
        {SyncType::Sound, TransString::getTransString(STRING_SOUND), "dcc_cloud_voice", true},
        {SyncType::Theme, TransString::getTransString(STRING_THEME), "dcc_cloud_them", true},
        {SyncType::Power, TransString::getTransString(STRING_POWER), "dcc_cloud_power", true},
        {SyncType::Network, TransString::getTransString(STRING_NETWORK), "dcc_cloud_ net", true},
        {SyncType::Mouse, TransString::getTransString(STRING_MOUSE), "dcc_cloud_mouse", true},
        {SyncType::Update, TransString::getTransString(STRING_UPDATE), "dcc_cloud_update", true},
        {SyncType::Dock, TransString::getTransString(STRING_DOCK), "dcc_cloud_taskbar", true},
        {SyncType::Launcher, TransString::getTransString(STRING_LAUNCHER), "dcc_cloud_luncher", true},
        {SyncType::Wallpaper, TransString::getTransString(STRING_WALLPAPER), "dcc_cloud_wallpaper", true},
    };

    initUI();
    initSysConfig();
    initConnection();
}

void CloudSyncPage::setSyncModel(SyncModel *syncModel)
{
    m_syncModel = syncModel;
    //
    connect(m_syncModel, &SyncModel::moduleSyncStateChanged, this, &CloudSyncPage::onModuleStateChanged);
    connect(m_syncModel, &SyncModel::syncAppsChanged, this, &CloudSyncPage::addSwitcherDumpDate);
    connect(m_syncModel, &SyncModel::utcloudSwitcherChange, this, &CloudSyncPage::onUtcloudModuleStateChanged);
    connect(m_syncModel, &SyncModel::enableSyncChanged, this, [this](bool benable){
        m_syncState = benable;
        m_autoSyncSwitch->setChecked(benable);
        this->onAutoSyncChanged(m_syncState);
    });
    connect(m_syncModel, &SyncModel::userInfoChanged, this, &CloudSyncPage::onUserInfoChanged);
    connect(m_syncModel, &SyncModel::licenseStateChanged, this, [ = ] (bool value) {
        if (!value)
        {
            this->makeContentDisable();
            this->makeSwitchDisable(TransString::getTransString(STRING_INACTIVE));
        }
    });
    connect(m_syncModel, &SyncModel::lastSyncTimeChanged, this, &CloudSyncPage::onLastSyncTimeChanged);

    m_autoSyncSwitch->setChecked(m_syncState);
    onAutoSyncChanged(m_syncState);
}

void CloudSyncPage::setSyncWorker(SyncWorker *worker)
{
    m_syncWorker = worker;
}

void CloudSyncPage::onLogin()
{
    if(!m_syncModel->getActivation())
    {
        makeContentDisable();
        makeSwitchDisable(TransString::getTransString(STRING_INACTIVE));
        return;
    }

    //init sync state
    bool userState = m_syncModel->enableSync() && m_syncModel->getActivation();
    qDebug() << "init sync state:" << userState;
    if(userState != m_syncState) {
        m_syncState = userState;
        m_autoSyncSwitch->setChecked(m_syncState);
        onAutoSyncChanged(m_syncState);
    }

    qDebug() << "init sysconfig";
    auto moduleMap = m_syncModel->moduleMap();
    for (auto &configItem: m_sysConfig)
    {
        bool syncState = m_syncModel->getModuleStateByType(SyncType(configItem.at(0).toInt()));
        configItem[3].setValue(QVariant::fromValue(syncState));
        //qDebug() << configItem[1].toString() << " set state:" << configItem[3].toBool();
    }

    //first init data
    QList<Apps *> tmpSyncItems = m_syncModel->syncItem();
    addSwitcherDumpDate(tmpSyncItems);
    //
    onLastSyncTimeChanged(m_syncModel->lastSyncTime());
    onUserInfoChanged(m_syncModel->userinfo());
}

void CloudSyncPage::onModuleStateChanged(std::pair<SyncType, bool> state)
{
    //qDebug() << "on module state changed:" << state.first << ", " << state.second;
    int index = 0;
    for (; index < m_sysConfig.size(); ++index)
    {
        if(SyncType(m_sysConfig.at(index).at(0).toInt()) == state.first)
        {
            break;
        }
    };

    if(index != m_sysConfig.size() && m_sysConfig[index][3].toBool() != state.second)
    {
        m_sysConfig[index][3].setValue(QVariant::fromValue(state.second));
        DStandardItem *syncItem = dynamic_cast<DStandardItem*>(m_configModel->item(index + 1));
        if(syncItem)
        {
            DViewItemAction *rightAction = syncItem->actionList(Qt::RightEdge).at(0);
            auto actionIcon = state.second ? DStyle::SP_IndicatorChecked : DStyle::SP_IndicatorUnchecked;
            rightAction->setIcon(qobject_cast<DStyle*>(style())->standardIcon(actionIcon));
            m_syncConfigView->update(syncItem->index());
        }
    }
}

void CloudSyncPage::onUtcloudModuleStateChanged(const QString &itemKey, bool state)
{
    qDebug() << "on utcloud module state change:" << state;
    if (!m_utcloudItemMap.contains(itemKey))
        return;

    auto item = static_cast<DStandardItem *>(m_utcloudItemMap[itemKey]);
    Q_ASSERT(item);

    auto action = item->actionList(Qt::Edge::RightEdge).first();
    auto checkstatus = state ? DStyle::SP_IndicatorChecked : DStyle::SP_IndicatorUnchecked;
    auto icon = qobject_cast<DStyle *>(style())->standardIcon(checkstatus);
    action->setIcon(icon);
    m_syncItemView->update(item->index());
}

void CloudSyncPage::onUserInfoChanged(const QVariantMap &infos)
{
    const QString region = infos["Region"].toString();
    if(!region.isEmpty()) {
        if(region != "CN") {
            qDebug() << "not in cn region:" << region;
            m_autoSyncSwitch->setChecked(false);
            makeSwitchDisable(TransString::getTransString(STRING_CANOTUSE));
        }
        else {
            qDebug() << "now in cn region";
            m_labelWarn->setVisible(false);
            m_autoSyncSwitch->setEnabled(true);
        }
    }
}

void CloudSyncPage::expandSysConfig(bool checked)
{
    Q_UNUSED(checked)
    DStandardItem *baseItem = dynamic_cast<DStandardItem*>(m_configModel->item(0));
    if(!baseItem)
    {
        return;
    }

    bool checkStatus = baseItem->data(Qt::UserRole + 100).toBool();
    if(!checkStatus)
    {
        //auto &&checkIcon = qobject_cast<DStyle*>(style())->standardIcon(DStyle::SP_ReduceElement);
        baseItem->actionList(Qt::LeftEdge).at(0)->setIcon(QIcon::fromTheme("go-down"));

        QSize size(20, 20);
        QMargins itemMargin(20, 10, 10, 6);
        for (auto &configItem: m_sysConfig)
        {
            DStandardItem *item = new DStandardItem;
            item->setSizeHint(QSize(-1, 37));
            item->setData(QVariant::fromValue(itemMargin), Dtk::MarginsRole);
            item->setText(configItem.at(1).toString());
            //QString theme = (Dtk::Gui::DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType) ? "_dark" : QString();
            //qInfo() << "config item icon:" << configItem.at(2).toString() + theme;
            item->setIcon(QIcon::fromTheme(configItem.at(2).toString()));
            item->setData(configItem.at(3), Qt::UserRole + 100);
            //qDebug() << configItem.at(1).toString() << ", set item data:" << configItem.at(3).toBool();

            auto&& rightAction = new DViewItemAction(Qt::AlignVCenter, size, size, false);
            auto&& checkoutState = configItem.at(3).toBool() ? DStyle::SP_IndicatorChecked : DStyle::SP_IndicatorUnchecked;
            auto&& checkIcon = qobject_cast<DStyle *>(style())->standardIcon(checkoutState);
            rightAction->setIcon(checkIcon);
            rightAction->setData(m_configModel->rowCount());
            item->setActionList(Qt::Edge::RightEdge, {rightAction});
            m_configModel->appendRow(item);
        }

        m_syncConfigView->setMinimumHeight((m_sysConfig.size() + 1) * 37);
        m_syncItemView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        m_syncItemView->setMinimumHeight(1);
    }
    else
    {
        //auto &&checkIcon = qobject_cast<DStyle*>(style())->standardIcon(DStyle::SP_ExpandElement);
        baseItem->actionList(Qt::LeftEdge).at(0)->setIcon(QIcon::fromTheme("go-next"));
        m_configModel->removeRows(1, m_configModel->rowCount() - 1);
        m_syncConfigView->setMinimumHeight(37);
        m_syncItemView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    }

    baseItem->setData(QVariant::fromValue(!checkStatus), Qt::UserRole + 100);
}

void CloudSyncPage::addSwitcherDumpDate(QList<Apps*> &appDate)
{
    QSize size(20, 20);
    for (auto &item: m_utcloudItemMap.values()) {
        m_itemModel->removeRow(item->row());
    }

    m_utcloudItemMap.clear();
    for (auto &app: appDate)
    {
        DStandardItem *item = new DStandardItem;
        QString itemText = app->name();
        bool visible = app->enable();
        item->setIcon(QIcon(app->icon()));
        item->setText(itemText);
        auto &&rightAction = new DViewItemAction(Qt::AlignVCenter, size, size, true);
        auto &&checkoutState = visible ? DStyle::SP_IndicatorChecked : DStyle::SP_IndicatorUnchecked;
        auto &&checkIcon = qobject_cast<DStyle *>(style())->standardIcon(checkoutState);
        rightAction->setIcon(checkIcon);
        item->setActionList(Qt::Edge::RightEdge, {rightAction});
        m_itemModel->appendRow(item);
        m_utcloudItemMap[app->datekey()] = item;
        qDebug() << "add sync app:" << itemText;
        connect(rightAction, &DViewItemAction::triggered, this, [=]{
            bool state = m_syncModel->getUtcloudStateByType(app->datekey());
            qDebug() << "app state:" << state;
            Q_EMIT requestSetUtcloudModuleState(app->datekey(), !state);
            auto actionIcon = !state ? DStyle::SP_IndicatorChecked : DStyle::SP_IndicatorUnchecked;
            rightAction->setIcon(qobject_cast<DStyle*>(style())->standardIcon(actionIcon));
        });
    }
}

void CloudSyncPage::onAutoSyncChanged(bool state)
{
    qDebug() << "on AutoSync:" << state;
    m_syncState = state;
    makeContentDisable(state);
    SyncTimeLblVisible(state && m_syncModel->lastSyncTime());
}

void CloudSyncPage::onLastSyncTimeChanged(const qlonglong lastSyncTime)
{
    if (lastSyncTime == 0) {
        m_syncTimeTips->hide();
        return;
    }

    m_syncTimeTips->setText(TransString::getTransString(STRING_LASTSYNC).arg(QDateTime::fromMSecsSinceEpoch(lastSyncTime * 1000)
             .toString("yyyy/MM/dd hh:mm")));
    SyncTimeLblVisible(true);
}

void CloudSyncPage::initSysConfig()
{
    QSize size(12, 12);
    //QSize bigSize(32, 32);
    QMargins itemMargin(20, 0, 10, 0);
    DStandardItem *item = new DStandardItem;
    item->setSizeHint(QSize(-1, 37));
    item->setData(QVariant::fromValue(itemMargin), Dtk::MarginsRole);
    item->setIcon(QIcon::fromTheme("dcc_cfg_set"));
    item->setText(TransString::getTransString(STRING_SYSCONFIG));
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    item->setData(QVariant::fromValue(false), Qt::UserRole + 100);

    auto&& leftAction = new DViewItemAction(Qt::AlignVCenter, size, size, true);
    leftAction->setIcon(QIcon::fromTheme("go-next"));
    item->setActionList(Qt::Edge::LeftEdge, {leftAction});

    auto &&rightAction = new DViewItemAction(Qt::AlignVCenter, QSize(), QSize(), true);
    rightAction->setText(TransString::getTransString(STRING_SELECTALL));
    rightAction->setTextColorRole(DPalette::Link);
    rightAction->setData(true);
    item->setActionList(Qt::Edge::RightEdge, {rightAction});
    m_configModel->appendRow(item);
    m_syncConfigView->setMinimumHeight(46);

    connect(leftAction, &DViewItemAction::triggered, this, &CloudSyncPage::expandSysConfig);
    connect(rightAction, &DViewItemAction::triggered, [=]{
        bool selected = rightAction->data().toBool();
        rightAction->setData(!selected);
        rightAction->setText(!selected ? TransString::getTransString(STRING_SELECTALL) : TransString::getTransString(STRING_UNSELECTALL));
        this->m_syncConfigView->update(this->m_configModel->index(0, 0));
        bool checkStatus = this->m_configModel->item(0)->data(Qt::UserRole + 100).toBool();
        if(!checkStatus)
        {
            Q_EMIT leftAction->triggered();
        }

        for (int i = 1; i < m_configModel->rowCount(); ++i)
        {
            DStandardItem *configItem = dynamic_cast<DStandardItem*>(m_configModel->item(i));
            if(configItem)
            {
                bool isChecked = configItem->data(Qt::UserRole + 100).toBool();
                qDebug() << "selected:" << selected << ", checked:" << isChecked;
                if(selected != isChecked)
                {
                    Q_EMIT m_syncConfigView->clicked(configItem->index());
                }
            }
        }

        this->m_syncConfigView->update();
    });
}

void CloudSyncPage::initUI()
{
    m_labelClear->setText(QString("<a style='text-decoration:none;'; href=' '>%1</a>").arg(TransString::getTransString(STRING_EMPTY)));
    m_labelClear->setOpenExternalLinks(false);

    setBackgroundRole(QPalette::Base);
    setFocusPolicy(Qt::FocusPolicy::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_autoSyncTips->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_autoSyncTips->setWordWrap(true);
    DFontSizeManager::instance()->bind(m_autoSyncTips, DFontSizeManager::T9, QFont::Thin);
    DFontSizeManager::instance()->bind(m_bindedTips, DFontSizeManager::T5, QFont::DemiBold);
    DFontSizeManager::instance()->bind(m_syncTimeTips, DFontSizeManager::T9, QFont::Thin);
    DFontSizeManager::instance()->bind(m_labelClear, DFontSizeManager::T9);

    m_labelWarn = new WarnLabel("");
    m_labelWarn->setPixmap(QIcon::fromTheme("dcc_not_use").pixmap(24, 24));
    m_labelWarn->setMouseTracking(true);
    m_labelWarn->setVisible(false);

    m_autoSyncSwitch->layout()->setContentsMargins(10, 0, 0, 0);
    m_mainlayout = new QVBoxLayout();
    QVBoxLayout *headlayout = new QVBoxLayout();
    QHBoxLayout *synclayout = new QHBoxLayout();
    QHBoxLayout *tiplayout = new QHBoxLayout();
    tiplayout->setContentsMargins(0, 0, 0, 0);
    tiplayout->addWidget(m_autoSyncTips);
    headlayout->setSpacing(6);
    headlayout->setContentsMargins(0, 0, 0, 0);
    synclayout->addWidget(m_bindedTips, 0, Qt::AlignLeft);
    synclayout->addStretch();
    synclayout->addWidget(m_labelWarn);
    synclayout->addWidget(m_autoSyncSwitch, 0, Qt::AlignRight);
    headlayout->addLayout(synclayout);
    headlayout->addLayout(tiplayout);

    m_mainlayout->setContentsMargins(20, 20, 20, 10);
    m_mainlayout->setSpacing(20);

    m_mainlayout->addLayout(headlayout);
    m_centerLayout = new QVBoxLayout;
    //同步系统配置
    m_syncConfigView->setBackgroundType(DStyledItemDelegate::BackgroundType::ClipCornerBackground);
    m_syncConfigView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_syncConfigView->setSelectionMode(QListView::SelectionMode::NoSelection);
    m_syncConfigView->setEditTriggers(DListView::NoEditTriggers);
    m_syncConfigView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_syncConfigView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_syncConfigView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_syncConfigView->setFrameShape(DListView::NoFrame);
    m_syncConfigView->setItemSpacing(1);
    m_syncConfigView->setViewportMargins(0, 0, 1, 0);
    m_syncConfigView->setModel(m_configModel);
    m_syncConfigView->setIconSize(QSize(16, 16));

    //同步项
    m_syncItemView->setBackgroundType(DStyledItemDelegate::BackgroundType::RoundedBackground);
    m_syncItemView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_syncItemView->setSelectionMode(QListView::SelectionMode::NoSelection);
    m_syncItemView->setEditTriggers(DListView::NoEditTriggers);
    m_syncItemView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_syncItemView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_syncItemView->setFrameShape(DListView::NoFrame);
    m_syncItemView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_syncItemView->setItemSpacing(10);
    m_syncItemView->setViewportMargins(0, 0, 1, 0);
    m_syncItemView->setModel(m_itemModel);
    m_syncItemView->setIconSize(QSize(24, 24));

    //底部
    m_labelClear->setForegroundRole(QPalette::Link);
    QVBoxLayout *bottomLayout = new QVBoxLayout;
    bottomLayout->setSpacing(3);
    QHBoxLayout *bottomTimeLayout = new QHBoxLayout();
    m_btnLine = new DHorizontalLine();
    bottomTimeLayout->addWidget(m_syncTimeTips, 0, Qt::AlignLeft);
    bottomTimeLayout->addStretch();
    bottomTimeLayout->addWidget(m_labelClear, 0, Qt::AlignRight);
    bottomLayout->addWidget(m_btnLine);
    bottomLayout->addLayout(bottomTimeLayout);

    m_centerLayout->setSpacing(10);
    m_centerLayout->setContentsMargins(0, 0, 0, 0);
    m_centerLayout->addWidget(m_syncConfigView);
    m_centerLayout->addWidget(m_syncItemView);
    m_centerLayout->addStretch();

    m_mainlayout->addLayout(m_centerLayout);
    m_mainlayout->addSpacing(-17);
    m_mainlayout->addLayout(bottomLayout);
    setLayout(m_mainlayout);
    //init dialog
    m_clearDlg->setFixedWidth(400);
    m_clearDlg->setIcon(QIcon::fromTheme("deepin-browser"));
    m_clearDlg->addButton(TransString::getTransString(STRING_CANCEL));
    m_clearDlg->addButton(TransString::getTransString(STRING_CLEARBTN), true, DDialog::ButtonWarning);
    m_clearDlg->setOnButtonClickedClose(true);
}

void CloudSyncPage::initConnection()
{
    connect(m_labelClear, &QLabel::linkActivated, this, [this]{
        this->m_clearDlg->exec();
    });

    connect(m_autoSyncSwitch, &SwitchWidget::checkedChanged, this, &CloudSyncPage::onAutoSyncChanged);
    connect(m_autoSyncSwitch, &SwitchWidget::checkedChanged, this, &CloudSyncPage::requestSetAutoSync);
    connect(m_clearDlg, &QDialog::accepted, this, &CloudSyncPage::checkPassword);
    connect(this, &CloudSyncPage::onUserLogout, m_clearDlg, &QDialog::reject);
    connect(m_syncConfigView, &QListView::clicked, [=](const QModelIndex &index) {
        int rowNum = index.row();
        if(rowNum == 0) {
            return;
        }

        auto confItem = dynamic_cast<DStandardItem*>(this->m_configModel->itemFromIndex(index));
        //qDebug() << "item:" << confItem << ", row:" << rowNum;
        auto rightAction = confItem->actionList(Qt::RightEdge).at(0);
        if(confItem)
        {
            bool state = confItem->data(Qt::UserRole + 100).toBool();
            confItem->setData(!state, Qt::UserRole + 100);
            qDebug() << confItem->data(Qt::DisplayRole).toString() << " , is checked:" << !state;
            this->m_sysConfig[rowNum - 1][3].setValue(QVariant::fromValue(!state));
            auto syncModuleMap = this->m_syncModel->moduleMap();
            SyncType itemType = SyncType(this->m_sysConfig[rowNum - 1].at(0).toInt());
            auto iter = syncModuleMap.begin();
            for (; iter != syncModuleMap.end(); ++iter)
            {
                if(iter->first == itemType)
                    break;
            }

            if(iter != syncModuleMap.end())
            {
                //qDebug() << "emit set state:" << confItem->data(Qt::DisplayRole).toString() << ", " << !state;
                for(const QString &value: iter->second)
                {
                    Q_EMIT this->requestSetModuleState(value, !state);
                }
            }

            auto actionIcon = !state ? DStyle::SP_IndicatorChecked : DStyle::SP_IndicatorUnchecked;
            rightAction->setIcon(qobject_cast<DStyle*>(style())->standardIcon(actionIcon));
            //qDebug() << "update icon:" << actionIcon << ", index:" << confItem->index().row();
            this->m_syncConfigView->update(confItem->index());
        }
    });
}

void CloudSyncPage::SyncTimeLblVisible(bool isVisible)
{
    if (!m_autoSyncSwitch->checked() || !m_autoSyncSwitch->switchButton()->isEnabled()) {
        m_syncTimeTips->setVisible(false);
        return;
    }

    m_syncTimeTips->setVisible(isVisible);
}

void CloudSyncPage::makeContentDisable(bool enable)
{
    if(enable)
    {
        m_labelWarn->setVisible(false);
        m_syncConfigView->setEnabled(true);
        m_syncItemView->setEnabled(true);
        m_btnLine->setVisible(true);
        m_syncTimeTips->setVisible(true);
        m_labelClear->setVisible(true);
        m_autoSyncSwitch->setEnabled(true);
    }
    else
    {
        m_syncConfigView->setEnabled(false);
        m_syncItemView->setEnabled(false);
        m_btnLine->setVisible(false);
        m_syncTimeTips->setVisible(false);
        m_labelClear->setVisible(false);
    }
}

void CloudSyncPage::makeSwitchDisable(const QString &tiptext)
{
    m_labelWarn->SetTipText(tiptext);
    m_labelWarn->setVisible(true);
    m_autoSyncSwitch->setEnabled(false);
}

void CloudSyncPage::initRegisterDialog(RegisterDlg *dlg)
{
    connect(dlg, &RegisterDlg::registerPasswd, this, [=](const QString &strpwd){
        m_syncWorker->registerPasswd(strpwd);
        dlg->accept();
    });
    connect(this, &CloudSyncPage::onUserLogout, dlg, &QDialog::reject);
}

void CloudSyncPage::initVerifyDialog(VerifyDlg *dlg)
{
    connect(dlg, &VerifyDlg::verifyPasswd, [=](const QString &strpwd){
        QString encryptPwd;
        int remainNum = 0;
        if(m_syncWorker->checkPassword(strpwd, encryptPwd, remainNum))
        {
            qInfo() << __LINE__ << "check password success";
            dlg->accept();
        }
        else
        {
            qInfo() << __LINE__ << "check password failed";
            //tip passwd is wrong
            dlg->showAlert(utils::getRemainPasswdMsg(remainNum));
        }
    });
    connect(dlg, &VerifyDlg::forgetPasswd, [this]{
        m_syncWorker->openForgetPasswd(utils::forgetPwdURL());
    });
    connect(this, &CloudSyncPage::onUserLogout, dlg, &QDialog::reject);
}

void CloudSyncPage::checkPassword()
{
    int ret;
    //check password is empty
    bool isEmpty = false;
    if(!m_syncWorker->checkPasswdEmpty(isEmpty))
    {
        utils::sendSysNotify(TransString::getTransString(STRING_FAILTIP));
        qWarning() << "check password empty failed";
        return;
    }

    if(isEmpty)
    {
        //password is empty
        RegisterDlg regDlg;
        initRegisterDialog(&regDlg);
        ret = regDlg.exec();
    }
    else
    {
        VerifyDlg verifyDlg;
        initVerifyDialog(&verifyDlg);
        ret = verifyDlg.exec();
    }

    if(ret == QDialog::Accepted)
    {
        qInfo() << "on accept clear cloud data";
        Q_EMIT clearCloudData();
    }
}

WarnLabel::WarnLabel(const QString &text, QWidget *parent):DLabel(text, parent)
{
    ;
}

void WarnLabel::enterEvent(QEvent *event)
{
    if(event->type() == QEvent::Enter)
    {
        auto tooltip = new DToolTip(m_tipText);
        QPoint globalPos = parentWidget()->mapToGlobal(geometry().topLeft());

        globalPos.setX(globalPos.x() - (tooltip->sizeHint().width() / 2));
        globalPos.setY(globalPos.y() + height());

        tooltip->show(globalPos, 1000);
    }
}

SyncConfigModel::SyncConfigModel(QObject *parent): QStandardItemModel(parent)
{
    ;
}

QVariant SyncConfigModel::data(const QModelIndex &index, int role) const
{
    if(role == Qt::DecorationRole && index.row() == 0) {
        QVariant imgdata = QStandardItemModel::data(index, role);
        if(imgdata.canConvert<QIcon>()) {
            QPixmap bigIcon = imgdata.value<QIcon>().pixmap(24, 24);
            return bigIcon;
        }
    }

    return QStandardItemModel::data(index, role);
}