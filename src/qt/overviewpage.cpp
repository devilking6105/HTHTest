// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Proton Core developers
// Copyright (c) 2018 The HTH Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "utilitydialog.h"
#include "walletmodel.h"
#include "main.h"
#include "chainparams.h"
#include "main.h"
#include "amount.h"
#include "addressbookpage.h"
#include "rpcblockchain.cpp"

#include "darksend.h"
#include "instantx.h"
#include "darksendconfig.h"
#include "masternode-sync.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(const PlatformStyle *platformStyle):
        QAbstractItemDelegate(), unit(BitcoinUnits::HTH),
        platformStyle(platformStyle)
    {

    }

    inline void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        QSettings settings;
        QString theme = settings.value("theme", "drkblue").toString();

        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        mainRect.moveLeft(ICON_OFFSET);
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE - 6, DECORATION_SIZE - 6));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2 * ypad) / 2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top() + ypad, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top() + ypad + halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        //QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = COLOR_BLACK;

        // paint address
        if (theme.operator==("light")) foreground = QColor(140, 104, 76); //"#8C684C"
        else if (theme.operator==("drkblue")) foreground = QColor(205, 220, 234);
        else foreground = COLOR_BLACK;
        //if (value.canConvert<QBrush>()) {
        //    QBrush brush = qvariant_cast<QBrush>(value);
        //    foreground = brush.color();
        //}
        //if (theme.operator==("light")) foreground = QColor(144, 144, 144);
        //else if (theme.operator==("drkblue")) foreground = QColor(103, 119, 127);
        //else foreground = foreground;
        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter, address, &boundingRect);

        // paint amount
        if (amount < 0) {
            if (theme.operator==("light")) foreground = QColor(220, 50, 50); //"#DC3232"
            else if (theme.operator==("drkblue")) foreground = QColor(220, 50, 50);
            else foreground = COLOR_NEGATIVE;
        } else if (!confirmed) {
            if (theme.operator==("light")) foreground = QColor(151, 135, 117); //"#978775"
            else if (theme.operator==("drkblue")) foreground = QColor(205, 220, 234);
            else foreground = COLOR_UNCONFIRMED;
        } else {
            if (theme.operator==("light")) foreground = QColor(240, 216, 174); //"#F0D8AE"
            else if (theme.operator==("drkblue")) foreground = QColor(205, 220, 234);
            else foreground = COLOR_BLACK;
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if (!confirmed) {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, amountText);

        // paint date
        if (theme.operator==("light")) foreground = QColor(240, 216, 174); //"#F0D8AE"
        else if (theme.operator==("drkblue")) foreground = QColor(205, 220, 234);
        else foreground = COLOR_BLACK;
        painter->drawText(amountRect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;
};


#include "overviewpage.moc"

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    txdelegate(new TxViewDelegate(platformStyle)),
    filter(0)
{
        
    ui->setupUi(this);
    QString theme = GUIUtil::getThemeName();
        
   /* ui->pushButton_Website->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/website"))); */
    ui->pushButton_Website->setStatusTip(tr("Visit Help The Homeless Worldwide A NJ Nonprofit Corporation"));
    ui->pushButton_Website_1->setStatusTip(tr("Visit Help The Homeless Coin"));
        
    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    // Note: minimum height of listTransactions will be set later in updateAdvancedPSUI() to reflect actual settings
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelPrivateSendSyncStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // hide PS frame (helps to preserve saved size)
    // we'll setup and make it visible in updateAdvancedPSUI() later if we are not in litemode
    ui->togglePrivateSend_2->setVisible(false);

         //information block update
       
    timerinfo_blockchain = new QTimer(this);
    connect(timerinfo_blockchain, SIGNAL(timeout()), this, SLOT(updateBlockChainInfo()));
    timerinfo_blockchain->start(1000); //30sec
        
        
    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

    // that's it for litemode
    if(fLiteMode) return;

    // Disable any PS UI for masternode or when autobackup is disabled or failed for whatever reason
    if(fMasterNode || nWalletBackups <= 0){
        DisablePrivateSendCompletely();
        if (nWalletBackups <= 0) {
            ui->labelPrivateSendEnabled_2->setToolTip(tr("Automatic backups are disabled, no mixing available!"));
        }
    } else {
        if(!fEnablePrivateSend){
            ui->labelPrivateSendEnabled_2->setText(tr("Start Mixing"));
        } else {
            ui->labelPrivateSendEnabled_2->setText(tr("Stop Mixing"));
        }
        // Disable darkSendPool builtin support for automatic backups while we are in GUI,
        // we'll handle automatic backups and user warnings in privateSendStatus()
        darkSendPool.fCreateAutoBackups = false;

        timer = new QTimer(this);
        connect(timer, SIGNAL(timeout()), this, SLOT(privateSendStatus()));
        timer->start(1000);
    }
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    if(!fLiteMode && !fMasterNode) disconnect(timer, SIGNAL(timeout()), this, SLOT(privateSendStatus()));
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& anonymizedBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentAnonymizedBalance = anonymizedBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    ui->labelBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmatureText->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelAnonymized_2->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, anonymizedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotalText->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, balance + unconfirmedBalance + immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance

    updatePrivateSendProgress();

    static int cachedTxLocks = 0;

    if(cachedTxLocks != nCompleteTXLocks){
        cachedTxLocks = nCompleteTXLocks;
        ui->listTransactions->update();
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelBalance->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelTotal->setVisible(showWatchOnly);      // show watch-only label
    ui->labelBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelTotal->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelTotal->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly){
        ui->labelWatchImmature->hide();
    }
    else{
        ui->labelBalance->setIndent(20);
        ui->labelUnconfirmed->setIndent(20);
        ui->labelImmatureText->setIndent(20);
        ui->labelTotalText->setIndent(20);
    }
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // update the display unit, to not use the default ("HTH")
        updateDisplayUnit();
        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getAnonymizedBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(privateSendRoundsChanged()), this, SLOT(updatePrivateSendProgress()));
        connect(model->getOptionsModel(), SIGNAL(privateSentAmountChanged()), this, SLOT(updatePrivateSendProgress()));
        connect(model->getOptionsModel(), SIGNAL(advancedPSUIChanged(bool)), this, SLOT(updateAdvancedPSUI(bool)));
        // explicitly update PS frame and transaction list to reflect actual settings
        updateAdvancedPSUI(model->getOptionsModel()->getShowAdvancedPSUI());

        connect(ui->privateSendAuto_2, SIGNAL(clicked()), this, SLOT(privateSendAuto()));
        connect(ui->privateSendReset_2, SIGNAL(clicked()), this, SLOT(privateSendReset()));
        connect(ui->privateSendInfo_2, SIGNAL(clicked()), this, SLOT(privateSendInfo()));
        connect(ui->togglePrivateSend_2, SIGNAL(clicked()), this, SLOT(togglePrivateSend()));
        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentAnonymizedBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString& warnings)
{
  //  this->ui->labelAlerts->setVisible(!warnings.isEmpty());
  //  this->ui->labelAlerts->setText(warnings);
}


                /**** Blockchain Information *****/

void OverviewPage::updateBlockChainInfo()
{
    if (masternodeSync.IsBlockchainSynced())
    {
        int CurrentBlock = clientModel->getNumBlocks();
      /*  int64_t netHashRate = chainActive.GetNetworkHashPS(24, CurrentBlock-1); */
      /*  double BlockReward = GetBlockHash(CurrentBlock); */
        double BlockRewardHTH =  static_cast<double>(BlockRewardHTH/COIN);
        double CurrentDiff = GetDifficulty();

        ui->label_CurrentBlock_value_3->setText(QString::number(CurrentBlock));
        ui->label_Nethash_3->setText(tr("Difficulty:"));
        ui->label_Nethash_value_3->setText(QString::number(CurrentDiff,'f',4));
        ui->label_CurrentBlockReward_value_3->setText(QString::number(BlockRewardHTH, 'f', 1));
      /*  ui->label_24hBlock_value_3->setText(QString::number(block24hCount));  */
  
    }
}

                /**** End Blockchain Information ******/


void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelPrivateSendSyncStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::updatePrivateSendProgress()
{
    if(!masternodeSync.IsBlockchainSynced() || ShutdownRequested()) return;

    if(!pwalletMain) return;

    QString strAmountAndRounds;
    QString strPrivateSendAmount = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, nPrivateSendAmount * COIN, false, BitcoinUnits::separatorAlways);

    if(currentBalance == 0)
    {
        ui->privateSendProgress_2->setValue(0);
        ui->privateSendProgress_2->setToolTip(tr("No inputs detected"));

        // when balance is zero just show info from settings
        strPrivateSendAmount = strPrivateSendAmount.remove(strPrivateSendAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strPrivateSendAmount + " / " + tr("%n Rounds", "", nPrivateSendRounds);

        ui->labelAmountRounds_2->setToolTip(tr("No inputs detected"));
        ui->labelAmountRounds_2->setText(strAmountAndRounds);
        return;
    }

    CAmount nDenominatedConfirmedBalance;
    CAmount nDenominatedUnconfirmedBalance;
    CAmount nAnonymizableBalance;
    CAmount nNormalizedAnonymizedBalance;
    double nAverageAnonymizedRounds;

    {
        nDenominatedConfirmedBalance = pwalletMain->GetDenominatedBalance();
        nDenominatedUnconfirmedBalance = pwalletMain->GetDenominatedBalance(true);
        nAnonymizableBalance = pwalletMain->GetAnonymizableBalance();
        nNormalizedAnonymizedBalance = pwalletMain->GetNormalizedAnonymizedBalance();
        nAverageAnonymizedRounds = pwalletMain->GetAverageAnonymizedRounds();
    }

    CAmount nMaxToAnonymize = nAnonymizableBalance + currentAnonymizedBalance + nDenominatedUnconfirmedBalance;

    // If it's more than the anon threshold, limit to that.
    if(nMaxToAnonymize > nPrivateSendAmount*COIN) nMaxToAnonymize = nPrivateSendAmount*COIN;

    if(nMaxToAnonymize == 0) return;

    if(nMaxToAnonymize >= nPrivateSendAmount * COIN) {
        ui->labelAmountRounds_2->setToolTip(tr("Found enough compatible inputs to anonymize %1")
                                          .arg(strPrivateSendAmount));
        strPrivateSendAmount = strPrivateSendAmount.remove(strPrivateSendAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strPrivateSendAmount + " / " + tr("%n Rounds", "", nPrivateSendRounds);
    } else {
        QString strMaxToAnonymize = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, nMaxToAnonymize, false, BitcoinUnits::separatorAlways);
        ui->labelAmountRounds_2->setToolTip(tr("Not enough compatible inputs to anonymize <span style='color:red;'>%1</span>,<br>"
                                             "will anonymize <span style='color:red;'>%2</span> instead")
                                          .arg(strPrivateSendAmount)
                                          .arg(strMaxToAnonymize));
        strMaxToAnonymize = strMaxToAnonymize.remove(strMaxToAnonymize.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = "<span style='color:red;'>" +
                QString(BitcoinUnits::factor(nDisplayUnit) == 1 ? "" : "~") + strMaxToAnonymize +
                " / " + tr("%n Rounds", "", nPrivateSendRounds) + "</span>";
    }
    ui->labelAmountRounds_2->setText(strAmountAndRounds);

    // calculate parts of the progress, each of them shouldn't be higher than 1
    // progress of denominating
    float denomPart = 0;
    // mixing progress of denominated balance
    float anonNormPart = 0;
    // completeness of full amount anonymization
    float anonFullPart = 0;

    CAmount denominatedBalance = nDenominatedConfirmedBalance + nDenominatedUnconfirmedBalance;
    denomPart = (float)denominatedBalance / nMaxToAnonymize;
    denomPart = denomPart > 1 ? 1 : denomPart;
    denomPart *= 100;

    anonNormPart = (float)nNormalizedAnonymizedBalance / nMaxToAnonymize;
    anonNormPart = anonNormPart > 1 ? 1 : anonNormPart;
    anonNormPart *= 100;

    anonFullPart = (float)currentAnonymizedBalance / nMaxToAnonymize;
    anonFullPart = anonFullPart > 1 ? 1 : anonFullPart;
    anonFullPart *= 100;

    // apply some weights to them ...
    float denomWeight = 1;
    float anonNormWeight = nPrivateSendRounds;
    float anonFullWeight = 2;
    float fullWeight = denomWeight + anonNormWeight + anonFullWeight;
    // ... and calculate the whole progress
    float denomPartCalc = ceilf((denomPart * denomWeight / fullWeight) * 100) / 100;
    float anonNormPartCalc = ceilf((anonNormPart * anonNormWeight / fullWeight) * 100) / 100;
    float anonFullPartCalc = ceilf((anonFullPart * anonFullWeight / fullWeight) * 100) / 100;
    float progress = denomPartCalc + anonNormPartCalc + anonFullPartCalc;
    if(progress >= 100) progress = 100;

    ui->privateSendProgress_2->setValue(progress);

    QString strToolPip = ("<b>" + tr("Overall progress") + ": %1%</b><br/>" +
                          tr("Denominated") + ": %2%<br/>" +
                          tr("Mixed") + ": %3%<br/>" +
                          tr("Anonymized") + ": %4%<br/>" +
                          tr("Denominated inputs have %5 of %n rounds on average", "", nPrivateSendRounds))
            .arg(progress).arg(denomPart).arg(anonNormPart).arg(anonFullPart)
            .arg(nAverageAnonymizedRounds);
    ui->privateSendProgress_2->setToolTip(strToolPip);
}

void OverviewPage::updateAdvancedPSUI(bool fShowAdvancedPSUI) {
    this->fShowAdvancedPSUI = fShowAdvancedPSUI;
    int nNumItems = (fLiteMode || !fShowAdvancedPSUI) ? NUM_ITEMS : NUM_ITEMS_ADV;
    SetupTransactionList(nNumItems);

    if (fLiteMode) return;

    ui->togglePrivateSend_2->setVisible(true);
    ui->labelCompletitionText_2->setVisible(fShowAdvancedPSUI);
    ui->privateSendProgress_2->setVisible(fShowAdvancedPSUI);
    ui->labelSubmittedDenomText_2->setVisible(fShowAdvancedPSUI);
    ui->labelSubmittedDenom_2->setVisible(fShowAdvancedPSUI);
    ui->privateSendAuto_2->setVisible(fShowAdvancedPSUI);
    ui->privateSendReset_2->setVisible(fShowAdvancedPSUI);
    ui->privateSendInfo_2->setVisible(true);
    ui->labelPrivateSendLastMessage_2->setVisible(fShowAdvancedPSUI);
}

void OverviewPage::privateSendStatus()
{
    if(!masternodeSync.IsBlockchainSynced() || ShutdownRequested()) return;

    static int64_t nLastDSProgressBlockTime = 0;
    int nBestHeight = clientModel->getNumBlocks();

    // We are processing more then 1 block per second, we'll just leave
    if(((nBestHeight - darkSendPool.nCachedNumBlocks) / (GetTimeMillis() - nLastDSProgressBlockTime + 1) > 1)) return;
    nLastDSProgressBlockTime = GetTimeMillis();

    QString strKeysLeftText(tr("keys left: %1").arg(pwalletMain->nKeysLeftSinceAutoBackup));
    if(pwalletMain->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING) {
        strKeysLeftText = "<span style='color:red;'>" + strKeysLeftText + "</span>";
    }
    ui->labelPrivateSendEnabled_2->setToolTip(strKeysLeftText);

    if (!fEnablePrivateSend) {
        if (nBestHeight != darkSendPool.nCachedNumBlocks) {
            darkSendPool.nCachedNumBlocks = nBestHeight;
            updatePrivateSendProgress();
        }

        ui->labelPrivateSendLastMessage_2->setText("");
        ui->togglePrivateSend_2->setText(tr("Start Mixing"));

        QString strEnabled = tr("Disabled");
        // Show how many keys left in advanced PS UI mode only
        if (fShowAdvancedPSUI) strEnabled += ", " + strKeysLeftText;
        ui->labelPrivateSendEnabled_2->setText(strEnabled);

        return;
    }

    // Warn user that wallet is running out of keys
    // NOTE: we do NOT warn user and do NOT create autobackups if mixing is not running
    if (nWalletBackups > 0 && pwalletMain->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING) {
        QSettings settings;
        if(settings.value("fLowKeysWarning").toBool()) {
            QString strWarn =   tr("Very low number of keys left since last automatic backup!") + "<br><br>" +
                                tr("We are about to create a new automatic backup for you, however "
                                   "<span style='color:red;'> you should always make sure you have backups "
                                   "saved in some safe place</span>!") + "<br><br>" +
                                tr("Note: You turn this message off in options.");
            ui->labelPrivateSendEnabled_2->setToolTip(strWarn);
            LogPrintf("OverviewPage::privateSendStatus -- Very low number of keys left since last automatic backup, warning user and trying to create new backup...\n");
            QMessageBox::warning(this, tr("PrivateSend"), strWarn, QMessageBox::Ok, QMessageBox::Ok);
        } else {
            LogPrintf("OverviewPage::privateSendStatus -- Very low number of keys left since last automatic backup, skipping warning and trying to create new backup...\n");
        }

        std::string strBackupWarning;
        std::string strBackupError;
        if(!AutoBackupWallet(pwalletMain, "", strBackupWarning, strBackupError)) {
            if (!strBackupWarning.empty()) {
                // It's still more or less safe to continue but warn user anyway
                LogPrintf("OverviewPage::privateSendStatus -- WARNING! Something went wrong on automatic backup: %s\n", strBackupWarning);

                QMessageBox::warning(this, tr("PrivateSend"),
                    tr("WARNING! Something went wrong on automatic backup") + ":<br><br>" + strBackupWarning.c_str(),
                    QMessageBox::Ok, QMessageBox::Ok);
            }
            if (!strBackupError.empty()) {
                // Things are really broken, warn user and stop mixing immediately
                LogPrintf("OverviewPage::privateSendStatus -- ERROR! Failed to create automatic backup: %s\n", strBackupError);

                QMessageBox::warning(this, tr("PrivateSend"),
                    tr("ERROR! Failed to create automatic backup") + ":<br><br>" + strBackupError.c_str() + "<br>" +
                    tr("Mixing is disabled, please close your wallet and fix the issue!"),
                    QMessageBox::Ok, QMessageBox::Ok);
            }
        }
    }

    QString strEnabled = fEnablePrivateSend ? tr("Enabled") : tr("Disabled");
    // Show how many keys left in advanced PS UI mode only
    if(fShowAdvancedPSUI) strEnabled += ", " + strKeysLeftText;
    ui->labelPrivateSendEnabled_2->setText(strEnabled);

    if(nWalletBackups == -1) {
        // Automatic backup failed, nothing else we can do until user fixes the issue manually
        DisablePrivateSendCompletely();

        QString strError =  tr("ERROR! Failed to create automatic backup") + ", " +
                            tr("see debug.log for details.") + "<br><br>" +
                            tr("Mixing is disabled, please close your wallet and fix the issue!");
        ui->labelPrivateSendEnabled_2->setToolTip(strError);

        return;
    } else if(nWalletBackups == -2) {
        // We were able to create automatic backup but keypool was not replenished because wallet is locked.
        QString strWarning = tr("WARNING! Failed to replenish keypool, please unlock your wallet to do so.");
        ui->labelPrivateSendEnabled_2->setToolTip(strWarning);
    }

    // check darksend status and unlock if needed
    if(nBestHeight != darkSendPool.nCachedNumBlocks) {
        // Balance and number of transactions might have changed
        darkSendPool.nCachedNumBlocks = nBestHeight;
        updatePrivateSendProgress();
    }

    QString strStatus = QString(darkSendPool.GetStatus().c_str());

    QString s = tr("Last PrivateSend message:\n") + strStatus;

    if(s != ui->labelPrivateSendLastMessage_2->text())
        LogPrintf("OverviewPage::privateSendStatus -- Last PrivateSend message: %s\n", strStatus.toStdString());

    ui->labelPrivateSendLastMessage_2->setText(s);

    if(darkSendPool.nSessionDenom == 0){
        ui->labelSubmittedDenom_2->setText(tr("N/A"));
    } else {
        QString strDenom(darkSendPool.GetDenominationsToString(darkSendPool.nSessionDenom).c_str());
        ui->labelSubmittedDenom_2->setText(strDenom);
    }

}

void OverviewPage::privateSendAuto(){
    darkSendPool.DoAutomaticDenominating();
}

void OverviewPage::privateSendReset(){
    darkSendPool.ResetPool();

    QMessageBox::warning(this, tr("PrivateSend"),
        tr("PrivateSend was successfully reset."),
        QMessageBox::Ok, QMessageBox::Ok);
}

void OverviewPage::privateSendInfo(){
    HelpMessageDialog dlg(this, HelpMessageDialog::pshelp);
    dlg.exec();
}

void OverviewPage::togglePrivateSend(){
    QSettings settings;
    // Popup some information on first mixing
    QString hasMixed = settings.value("hasMixed").toString();
    if(hasMixed.isEmpty()){
        QMessageBox::information(this, tr("PrivateSend"),
                tr("If you don't want to see internal PrivateSend fees/transactions select \"Most Common\" as Type on the \"Transactions\" tab."),
                QMessageBox::Ok, QMessageBox::Ok);
        settings.setValue("hasMixed", "hasMixed");
    }
    if(!fEnablePrivateSend){
        CAmount nMinAmount = vecPrivateSendDenominations.back() + PRIVATESEND_COLLATERAL*4;
        if(currentBalance < nMinAmount){
            QString strMinAmount(BitcoinUnits::formatWithUnit(nDisplayUnit, nMinAmount));
            QMessageBox::warning(this, tr("PrivateSend"),
                tr("PrivateSend requires at least %1 to use.").arg(strMinAmount),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        // if wallet is locked, ask for a passphrase
        if (walletModel->getEncryptionStatus() == WalletModel::Locked)
        {
            WalletModel::UnlockContext ctx(walletModel->requestUnlock(true));
            if(!ctx.isValid())
            {
                //unlock was cancelled
                darkSendPool.nCachedNumBlocks = std::numeric_limits<int>::max();
                QMessageBox::warning(this, tr("PrivateSend"),
                    tr("Wallet is locked and user declined to unlock. Disabling PrivateSend."),
                    QMessageBox::Ok, QMessageBox::Ok);
                LogPrint("privatesend", "OverviewPage::togglePrivateSend -- Wallet is locked and user declined to unlock. Disabling PrivateSend.\n");
                return;
            }
        }

    }

    fEnablePrivateSend = !fEnablePrivateSend;
    darkSendPool.nCachedNumBlocks = std::numeric_limits<int>::max();

    if(!fEnablePrivateSend){
        ui->togglePrivateSend_2->setText(tr("Start Mixing"));
        darkSendPool.UnlockCoins();
    } else {
        ui->togglePrivateSend_2->setText(tr("Stop Mixing"));

        /* show darksend configuration if client has defaults set */

        if(nPrivateSendAmount == 0){
            DarksendConfig dlg(this);
            dlg.setModel(walletModel);
            dlg.exec();
        }

    }
}

void OverviewPage::SetupTransactionList(int nNumItems) {
    ui->listTransactions->setMinimumHeight(nNumItems * (DECORATION_SIZE + 2));

    if(walletModel && walletModel->getOptionsModel()) {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(walletModel->getTransactionTableModel());
        filter->setLimit(nNumItems);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);
    }
}

void OverviewPage::DisablePrivateSendCompletely() {
    ui->togglePrivateSend_2->setText("(" + tr("Disabled") + ")");
    ui->privateSendAuto_2->setText("(" + tr("Disabled") + ")");
    ui->privateSendReset_2->setText("(" + tr("Disabled") + ")");
    ui->togglePrivateSend_2->setEnabled(false);
    if (nWalletBackups <= 0) {
        ui->labelPrivateSendEnabled_2->setText("<span style='color:red;'>(" + tr("Disabled") + ")</span>");
    }
    fEnablePrivateSend = false;
}


/************** HTH Worldwide Button ******************/
 
void OverviewPage::on_pushButton_Website_clicked() {  // Nonprofit Wesbite
    
    QDesktopServices::openUrl(QUrl("https://www.helpthehomelessworldwide.org/", QUrl::TolerantMode));
    
}

void OverviewPage::on_pushButton_Website_1_clicked() {  // HTH Coin Wesbite
    
    QDesktopServices::openUrl(QUrl("https://hth.world", QUrl::TolerantMode));
    
}


void OverviewPage::on_pushButton_Website_2_clicked() {  // HTH Exchanges
    
    QDesktopServices::openUrl(QUrl("https://hth.world/exchanges.html", QUrl::TolerantMode));
    
}

void OverviewPage::on_pushButton_Website_3_clicked() {  // HTH Explorer
    
    QDesktopServices::openUrl(QUrl("https://chainz.cryptoid.info/hth", QUrl::TolerantMode));
    
}

void OverviewPage::on_pushButton_Website_4_clicked() {  // HTH Partners
    
    QDesktopServices::openUrl(QUrl("https://hth.world/partners.html", QUrl::TolerantMode));
    
}

/************** HTH Worldwide Button ******************/

