// dlgtrackinfo.cpp
// Created 11/10/2009 by RJ Ryan (rryan@mit.edu)

#include <QtDebug>
#include <QFileDialog>
#include <QMenu>

#include "dlgtrackinfo.h"
#include "library/coverartcache.h"
#include "library/dao/cue.h"
#include "trackinfoobject.h"

const int kMinBPM = 30;
const int kMaxBPM = 240;
// Maximum allowed interval between beats in milli seconds (calculated from
// minBPM)
const int kMaxInterval = static_cast<int>(1000.0 * (60.0 / kMinBPM));

DlgTrackInfo::DlgTrackInfo(QWidget* parent,
                           DlgTagFetcher& DlgTagFetcher)
            : QDialog(parent),
              m_pLoadedTrack(NULL),
              m_DlgTagFetcher(DlgTagFetcher) {
    init();
}

DlgTrackInfo::~DlgTrackInfo() {
    unloadTrack(false);
    qDebug() << "~DlgTrackInfo()";
}

void DlgTrackInfo::init(){
    setupUi(this);

    cueTable->hideColumn(0);

    connect(btnNext, SIGNAL(clicked()),
            this, SLOT(slotNext()));
    connect(btnPrev, SIGNAL(clicked()),
            this, SLOT(slotPrev()));
    connect(btnApply, SIGNAL(clicked()),
            this, SLOT(apply()));
    connect(btnOK, SIGNAL(clicked()),
            this, SLOT(OK()));
    connect(btnCancel, SIGNAL(clicked()),
            this, SLOT(cancel()));

    connect(btnFetchTag, SIGNAL(clicked()),
            this, SLOT(fetchTag()));

    connect(bpmDouble, SIGNAL(clicked()),
            this, SLOT(slotBpmDouble()));
    connect(bpmHalve, SIGNAL(clicked()),
            this, SLOT(slotBpmHalve()));
    connect(bpmTwoThirds, SIGNAL(clicked()),
            this, SLOT(slotBpmTwoThirds()));
    connect(bpmThreeFourth, SIGNAL(clicked()),
            this, SLOT(slotBpmThreeFourth()));

    connect(btnCueActivate, SIGNAL(clicked()),
            this, SLOT(cueActivate()));
    connect(btnCueDelete, SIGNAL(clicked()),
            this, SLOT(cueDelete()));
    connect(bpmTap, SIGNAL(pressed()),
            this, SLOT(slotBpmTap()));
    connect(btnReloadFromFile, SIGNAL(clicked()),
            this, SLOT(reloadTrackMetadata()));
    m_bpmTapTimer.start();
    for (int i = 0; i < kFilterLength; ++i) {
        m_bpmTapFilter[i] = 0.0f;
    }

    connect(CoverArtCache::instance(), SIGNAL(pixmapFound(int)),
            this, SLOT(slotPixmapFound(int)));

    // Cover art actions
    // change cover art location
    QAction* changeCover = new QAction(tr("&Change"), this);
    connect(changeCover, SIGNAL(triggered()),
            this, SLOT(slotChangeCoverArt()));
    // unset cover art - load default
    QAction* unsetCover = new QAction(tr("&Unset"), this);
    connect(unsetCover, SIGNAL(triggered()),
            this, SLOT(slotUnsetCoverArt()));
    // reload just cover art from metadata
    QAction* reloadCover = new QAction(tr("Reload from &Metadata"), this);
    connect(reloadCover, SIGNAL(triggered()),
            this, SLOT(reloadEmbeddedCover()));
    // Cover art popup menu
    QMenu* coverMenu = new QMenu(this);
    coverMenu->addAction(changeCover);
    coverMenu->addAction(unsetCover);
    coverMenu->addAction(reloadCover);
    coverArt->setMenu(coverMenu);
}

void DlgTrackInfo::OK() {
    unloadTrack(true);
    accept();
}

void DlgTrackInfo::apply() {
    saveTrack();
}

void DlgTrackInfo::cancel() {
    unloadTrack(false);
    reject();
}

void DlgTrackInfo::trackUpdated() {

}

void DlgTrackInfo::slotNext() {
    emit(next());
}

void DlgTrackInfo::slotPrev() {
    emit(previous());
}

void DlgTrackInfo::cueActivate() {

}

void DlgTrackInfo::cueDelete() {
    QList<QTableWidgetItem*> selected = cueTable->selectedItems();
    QListIterator<QTableWidgetItem*> item_it(selected);

    QSet<int> rowsToDelete;
    while(item_it.hasNext()) {
        QTableWidgetItem* item = item_it.next();
        rowsToDelete.insert(item->row());
    }

    QList<int> rowsList = QList<int>::fromSet(rowsToDelete);
    qSort(rowsList);

    QListIterator<int> it(rowsList);
    it.toBack();
    while (it.hasPrevious()) {
        cueTable->removeRow(it.previous());
    }
}

void DlgTrackInfo::populateFields(TrackPointer pTrack) {
    setWindowTitle(pTrack->getArtist() % " - " % pTrack->getTitle());

    // Editable fields
    txtTrackName->setText(pTrack->getTitle());
    txtArtist->setText(pTrack->getArtist());
    txtAlbum->setText(pTrack->getAlbum());
    txtAlbumArtist->setText(pTrack->getAlbumArtist());
    txtGenre->setText(pTrack->getGenre());
    txtComposer->setText(pTrack->getComposer());
    txtGrouping->setText(pTrack->getGrouping());
    txtYear->setText(pTrack->getYear());
    txtTrackNumber->setText(pTrack->getTrackNumber());
    txtComment->setPlainText(pTrack->getComment());
    spinBpm->setValue(pTrack->getBpm());
    // Non-editable fields
    txtDuration->setText(pTrack->getDurationStr());
    txtLocation->setText(pTrack->getLocation());
    txtType->setText(pTrack->getType());
    txtBitrate->setText(QString(pTrack->getBitrateStr()) + (" ") + tr("kbps"));
    txtBpm->setText(pTrack->getBpmStr());
    txtKey->setText(pTrack->getKeyText());
    BeatsPointer pBeats = pTrack->getBeats();
    bool beatsSupportsSet = !pBeats || (pBeats->getCapabilities() & Beats::BEATSCAP_SET);
    bool enableBpmEditing = !pTrack->hasBpmLock() && beatsSupportsSet;
    spinBpm->setEnabled(enableBpmEditing);
    bpmTap->setEnabled(enableBpmEditing);
    bpmDouble->setEnabled(enableBpmEditing);
    bpmHalve->setEnabled(enableBpmEditing);
    bpmTwoThirds->setEnabled(enableBpmEditing);
    bpmThreeFourth->setEnabled(enableBpmEditing);
}

void DlgTrackInfo::loadTrack(TrackPointer pTrack) {
    m_pLoadedTrack = pTrack;
    clear();

    if (m_pLoadedTrack == NULL)
        return;

    populateFields(m_pLoadedTrack);
    populateCues(m_pLoadedTrack);

    // Load Default Cover Art
    coverArt->setIcon(CoverArtCache::instance()->getDefaultCoverArt());
    m_sLoadedCoverLocation.clear();
    m_sLoadedMd5Hash.clear();
}

void DlgTrackInfo::slotPixmapFound(int trackId) {
    if (m_pLoadedTrack == NULL)
        return;

    if (m_pLoadedTrack->getId() == trackId) {
        coverArt->setIcon(m_coverPixmap);
        update();
    }
}

void DlgTrackInfo::slotLoadCoverArt(const QString& coverLocation,
                                    const QString& md5Hash,
                                    int trackId) {
    m_coverPixmap = QPixmap();
    m_sLoadedCoverLocation = coverLocation;
    m_sLoadedMd5Hash = md5Hash;
    CoverArtCache::instance()->requestPixmap(trackId,
                                             m_coverPixmap,
                                             m_sLoadedCoverLocation,
                                             m_sLoadedMd5Hash);
}

void DlgTrackInfo::slotUnsetCoverArt() {
    if (m_pLoadedTrack == NULL) {
        return;
    }
    // TODO: get default cover location from CoverArtCache
    m_coverPixmap = QPixmap();
    bool res = CoverArtCache::instance()->changeCoverArt(
                            m_pLoadedTrack->getId(),
                            m_coverPixmap,
                            ":/images/library/vinyl-record.png");

    if (!res) {
        QMessageBox::warning(this, tr("Unset Cover Art"),
                             tr("Could not unset the cover art: '%s'"));
    }
}

void DlgTrackInfo::slotChangeCoverArt() {
    if (m_pLoadedTrack == NULL) {
        return;
    }

    QString dir;
    if (m_sLoadedCoverLocation.isEmpty()) {
        dir = m_pLoadedTrack->getDirectory();
    } else {
        dir = m_sLoadedCoverLocation;
    }
    QString newCoverLocation = QFileDialog::getOpenFileName(
                this, tr("Change Cover Art"), dir,
                tr("Image Files (*.png *.jpg *.jpeg *.bmp)"));

    if (newCoverLocation.isNull()) {
        return;
    }

    m_coverPixmap = QPixmap();
    bool res = CoverArtCache::instance()->changeCoverArt(
                m_pLoadedTrack->getId(), m_coverPixmap, newCoverLocation);
    if (!res) {
        QMessageBox::warning(this, tr("Change Cover Art"),
                             tr("Could not change the cover art: '%s'"));
    }
}

void DlgTrackInfo::populateCues(TrackPointer pTrack) {
    int sampleRate = pTrack->getSampleRate();

    QList<Cue*> listPoints;
    const QList<Cue*>& cuePoints = pTrack->getCuePoints();
    QListIterator<Cue*> it(cuePoints);
    while (it.hasNext()) {
        Cue* pCue = it.next();
        if (pCue->getType() == Cue::CUE || pCue->getType() == Cue::LOAD) {
            listPoints.push_back(pCue);
        }
    }
    it = QListIterator<Cue*>(listPoints);
    cueTable->setSortingEnabled(false);
    int row = 0;

    while (it.hasNext()) {
        Cue* pCue = it.next();

        QString rowStr = QString("%1").arg(row);

        // All hotcues are stored in Cue's as 0-indexed, but the GUI presents
        // them to the user as 1-indexex. Add 1 here. rryan 9/2010
        int iHotcue = pCue->getHotCue() + 1;
        QString hotcue = "";
        if (iHotcue != -1) {
            hotcue = QString("%1").arg(iHotcue);
        }

        int position = pCue->getPosition();
        double totalSeconds;
        if (position == -1)
            continue;
        else {
            totalSeconds = float(position) / float(sampleRate) / 2.0;
        }

        int fraction = 100*(totalSeconds - floor(totalSeconds));
        int seconds = int(totalSeconds) % 60;
        int mins = int(totalSeconds) / 60;
        //int hours = mins / 60; //Not going to worry about this for now. :)

        //Construct a nicely formatted duration string now.
        QString duration = QString("%1:%2.%3").arg(
            QString::number(mins),
            QString("%1").arg(seconds, 2, 10, QChar('0')),
            QString("%1").arg(fraction, 2, 10, QChar('0')));

        QTableWidgetItem* durationItem = new QTableWidgetItem(duration);
        // Make the duration read only
        durationItem->setFlags(Qt::NoItemFlags);

        m_cueMap[row] = pCue;
        cueTable->insertRow(row);
        cueTable->setItem(row, 0, new QTableWidgetItem(rowStr));
        cueTable->setItem(row, 1, durationItem);
        cueTable->setItem(row, 2, new QTableWidgetItem(hotcue));
        cueTable->setItem(row, 3, new QTableWidgetItem(pCue->getLabel()));
        row += 1;
    }
    cueTable->setSortingEnabled(true);
    cueTable->horizontalHeader()->setStretchLastSection(true);
}

void DlgTrackInfo::saveTrack() {
    if (!m_pLoadedTrack)
        return;

    m_pLoadedTrack->setTitle(txtTrackName->text());
    m_pLoadedTrack->setArtist(txtArtist->text());
    m_pLoadedTrack->setAlbum(txtAlbum->text());
    m_pLoadedTrack->setAlbumArtist(txtAlbumArtist->text());
    m_pLoadedTrack->setGenre(txtGenre->text());
    m_pLoadedTrack->setComposer(txtComposer->text());
    m_pLoadedTrack->setGrouping(txtGrouping->text());
    m_pLoadedTrack->setYear(txtYear->text());
    m_pLoadedTrack->setTrackNumber(txtTrackNumber->text());
    m_pLoadedTrack->setComment(txtComment->toPlainText());

    if (!m_pLoadedTrack->hasBpmLock()) {
        m_pLoadedTrack->setBpm(spinBpm->value());
    }

    QSet<int> updatedRows;
    for (int row = 0; row < cueTable->rowCount(); ++row) {
        QTableWidgetItem* rowItem = cueTable->item(row, 0);
        QTableWidgetItem* hotcueItem = cueTable->item(row, 2);
        QTableWidgetItem* labelItem = cueTable->item(row, 3);

        if (!rowItem || !hotcueItem || !labelItem)
            continue;

        int oldRow = rowItem->data(Qt::DisplayRole).toInt();
        Cue* pCue = m_cueMap.value(oldRow, NULL);
        if (pCue == NULL) {
            continue;
        }

        updatedRows.insert(oldRow);

        QVariant vHotcue = hotcueItem->data(Qt::DisplayRole);
        if (vHotcue.canConvert<int>()) {
            int iTableHotcue = vHotcue.toInt();
            // The GUI shows hotcues as 1-indexed, but they are actually
            // 0-indexed, so subtract 1
            pCue->setHotCue(iTableHotcue-1);
        } else {
            pCue->setHotCue(-1);
        }

        QString label = labelItem->data(Qt::DisplayRole).toString();
        pCue->setLabel(label);
    }

    QMutableHashIterator<int,Cue*> it(m_cueMap);
    // Everything that was not processed above was removed.
    while (it.hasNext()) {
        it.next();
        int oldRow = it.key();

        // If cue's old row is not in updatedRows then it must have been
        // deleted.
        if (updatedRows.contains(oldRow)) {
            continue;
        }
        Cue* pCue = it.value();
        it.remove();
        qDebug() << "Deleting cue" << pCue->getId() << pCue->getHotCue();
        m_pLoadedTrack->removeCue(pCue);
    }
}

void DlgTrackInfo::unloadTrack(bool save) {
    if (!m_pLoadedTrack)
        return;

    if (save) {
        saveTrack();
    }

    clear();
    m_pLoadedTrack.clear();
}

void DlgTrackInfo::clear() {

    txtTrackName->setText("");
    txtArtist->setText("");
    txtAlbum->setText("");
    txtAlbumArtist->setText("");
    txtGenre->setText("");
    txtComposer->setText("");
    txtGrouping->setText("");
    txtYear->setText("");
    txtTrackNumber->setText("");
    txtComment->setPlainText("");
    spinBpm->setValue(0.0);

    txtDuration->setText("");
    txtType->setText("");
    txtLocation->setText("");
    txtBitrate->setText("");
    txtBpm->setText("");

    m_cueMap.clear();
    cueTable->clearContents();
    cueTable->setRowCount(0);
}

void DlgTrackInfo::slotBpmDouble() {
    spinBpm->setValue(spinBpm->value() * 2.0);
}

void DlgTrackInfo::slotBpmHalve() {
    spinBpm->setValue(spinBpm->value() / 2.0);
}

void DlgTrackInfo::slotBpmTwoThirds() {
    spinBpm->setValue(spinBpm->value() * (2./3.));
}

void DlgTrackInfo::slotBpmThreeFourth() {
    spinBpm->setValue(spinBpm->value() * (3./4.));
}

void DlgTrackInfo::slotBpmTap() {
    int elapsed = m_bpmTapTimer.elapsed();
    m_bpmTapTimer.restart();

    if (elapsed <= kMaxInterval) {
        // Move back in filter one sample
        for (int i = 1; i < kFilterLength; ++i) {
            m_bpmTapFilter[i-1] = m_bpmTapFilter[i];
        }

        m_bpmTapFilter[kFilterLength-1] = 60000.0f/elapsed;
        if (m_bpmTapFilter[kFilterLength-1] > kMaxBPM)
            m_bpmTapFilter[kFilterLength-1] = kMaxBPM;

        double temp = 0.;
        for (int i = 0; i < kFilterLength; ++i) {
            temp += m_bpmTapFilter[i];
        }
        temp /= kFilterLength;
        spinBpm->setValue(temp);
    }
}

void DlgTrackInfo::reloadTrackMetadata() {
    if (m_pLoadedTrack) {
        TrackPointer pTrack(new TrackInfoObject(m_pLoadedTrack->getLocation(),
                                                m_pLoadedTrack->getSecurityToken()));
        populateFields(pTrack);
        reloadEmbeddedCover();
    }
}

void DlgTrackInfo::reloadEmbeddedCover() {
    CoverArtCache* covercache = CoverArtCache::instance();
    QString md5Hash = covercache->getHashOfEmbeddedCover(
                            m_pLoadedTrack->getLocation());

    QString msg;
    reloadCoverCases reloadCase = LOAD;
    if (md5Hash.isEmpty() && !m_sLoadedMd5Hash.isEmpty()) {
        reloadCase = UNSET;
        msg = tr("The current track does not have an embedded cover art.\n"
                 "Do you want to unset the current cover art?");
    } else if (md5Hash != m_sLoadedMd5Hash) {
        reloadCase = CHANGE;
        msg = tr("The current cover art is different from the embedded cover art.\n"
                 "Do you want to load the embedded cover art?");
    }

    if (reloadCase == LOAD) {
        if (!md5Hash.isEmpty()) {
            m_coverPixmap = QPixmap();
            m_sLoadedCoverLocation.clear();
            m_sLoadedMd5Hash = md5Hash;
            covercache->requestPixmap(m_pLoadedTrack->getId(), m_coverPixmap);
        }
        return;
    }

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Reload Embedded Cover Art"), msg,
                                  QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (reloadCase == UNSET) {
            slotUnsetCoverArt();
        } else if (reloadCase == CHANGE) {
            m_coverPixmap = QPixmap();
            m_sLoadedCoverLocation.clear();
            m_sLoadedMd5Hash = md5Hash;
            covercache->requestPixmap(m_pLoadedTrack->getId(), m_coverPixmap);
        }
    }
}

void DlgTrackInfo::fetchTag() {
    m_DlgTagFetcher.init(m_pLoadedTrack);
    m_DlgTagFetcher.show();
}
