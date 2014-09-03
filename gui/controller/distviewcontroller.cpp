#include "distviewcontroller.h"
#include "dist_view/viewport.h"
#include "widgets/mainwindow.h"

#include <QSignalMapper>

// for debug msgs
#include <boost/format.hpp>
#include <QDebug>

#include "subscriptions.h"

#define GGDBG_MODULE
#include <gerbil_gui_debug.h>

#define GGDBG_REPR(type) GGDBGM(format("%1%")%type << endl)


/** Holds a subscription set for representations.
 *
 * This is purely for avoiding the distviewcontroller.h -> subscriptions.h
 * dependency. Forward declaration of Subscription<representation::t>::Set
 * is impossible(?).
 */
struct ReprSubscriptions {
	Subscription<representation::t>::Set	repr;
};

DistViewController::DistViewController(Controller *ctrl,
									   BackgroundTaskQueue *taskQueue, ImageModel *im)
 : ctrl(ctrl), im(im), distviewSubs(new ReprSubscriptions)
{
	/* Create all viewers. Only one viewer per representation type is supported.
	 */
	foreach (representation::t rType, representation::all()) {
		payloadMap.insert(rType, new payload(rType));
		payloadMap[rType]->model.setTaskQueue(taskQueue);
		// TODO: the other way round. ctx+sets come from model
		payloadMap[rType]->model.setContext(payloadMap[rType]->gui.getContext());
		payloadMap[rType]->model.setBinSets(payloadMap[rType]->gui.getBinSets());
	}

	foreach (representation::t i, representation::all()) {
		distviewNeedsBinning[i] = false;
	}
}

DistViewController::~DistViewController()
{
	delete distviewSubs;
}

void DistViewController::init()
{
	// start with IMG, hide IMGPCA, GRADPCA at the beginning
	/* NOTE that we did not connect signals yet! This is actually good as we
	 * will not get stray signals before the content is ready.
	 */
	activeView = representation::IMG;
	payloadMap[activeView]->gui.setActive();
#ifdef WITH_IMGPCA
	payloadMap[representation::IMGPCA]->gui.toggleFold();
#endif /* WITH_IMGPCA */
#ifdef WITH_GRADPCA
	map[representation::GRADPCA]->gui.toggleFold();
#endif /* WITH_GRADPCA */

	connect(ctrl->imageModel(), SIGNAL(roiRectChanged(cv::Rect)),
			this, SLOT(processROIChage(cv::Rect)));
	connect(ctrl->imageModel(), SIGNAL(imageUpdate(representation::t,SharedMultiImgPtr,bool)),
			this, SLOT(processImageUpdate(representation::t,SharedMultiImgPtr,bool)));


	foreach (payload *p, payloadMap) {
		// fill GUI with distribution view
		ctrl->mainWindow()->addDistView(p->gui.getFrame());
	}

	foreach (payload *p, payloadMap) {
		DistViewModel *m = &p->model;

		connect(m, SIGNAL(newBinning(representation::t)),
				this, SLOT(processNewBinning(representation::t)));
		connect(m, SIGNAL(newBinningRange(representation::t)),
				this, SLOT(processNewBinningRange(representation::t)));

		DistViewGUI *g = &p->gui;
		// let GUI connect its member's signals to us
		g->initSignals(this);

		// connect signals from outside to GUI
		connect(ctrl, SIGNAL(showIlluminationCurve(bool)),
				g, SIGNAL(toggleIlluminationShown(bool)));

		connect(ctrl, SIGNAL(toggleSingleLabel(bool)),
				g, SLOT(toggleSingleLabel(bool)));

		connect(g, SIGNAL(needBinning(representation::t)),
				this, SLOT(processDistviewNeedsBinning(representation::t)));


		connect(g, SIGNAL(subscribeRepresentation(QObject*,representation::t)),
				this, SLOT(processDistviewSubscribeRepresentation(QObject*,representation::t)));
		connect(g, SIGNAL(unsubscribeRepresentation(QObject*,representation::t)),
				this, SLOT(processDistviewUnsubscribeRepresentation(QObject*,representation::t)));
	}

	/* connect pass-throughs / signals we process */
	connect(ctrl, SIGNAL(currentLabelChanged(int)),
			this, SLOT(setCurrentLabel(int)));
	connect(ctrl, SIGNAL(requestPixelOverlay(int,int)),
			this, SLOT(pixelOverlay(int,int)));
	connect(ctrl, SIGNAL(singleLabelSelected(int)),
			this, SIGNAL(singleLabelSelected(int)));
	connect(ctrl, SIGNAL(toggleIgnoreLabels(bool)),
			this, SLOT(toggleIgnoreLabels(bool)));

	/* connect illuminant correction stuff only to IMG distview */
	DistViewGUI *g = &payloadMap[representation::IMG]->gui;
	connect(this, SIGNAL(newIlluminantCurve(QVector<multi_img::Value>)),
			g, SIGNAL(newIlluminantCurve(QVector<multi_img::Value>)));
	connect(this, SIGNAL(toggleIlluminationShown(bool)),
			g, SIGNAL(toggleIlluminationShown(bool)));
	connect(this, SIGNAL(newIlluminantApplied(QVector<multi_img::Value>)),
			g, SIGNAL(newIlluminantApplied(QVector<multi_img::Value>)));

	/* model needs to know applied illuminant */
	connect(this, SIGNAL(newIlluminantApplied(QVector<multi_img::Value>)),
			&payloadMap[representation::IMG]->model,
			SLOT(setIlluminant(QVector<multi_img::Value>)));

	// forward representation subscriptions to controller
	connect(this, SIGNAL(subscribeRepresentation(QObject*,representation::t)),
			ctrl, SLOT(processSubscribeRepresentation(QObject*,representation::t)));
	connect(this, SIGNAL(unsubscribeRepresentation(QObject*,representation::t)),
			ctrl, SLOT(processUnsubscribeRepresentation(QObject*,representation::t)));
}

void DistViewController::initSubscriptions()
{
	foreach (payload *p, payloadMap) {
		DistViewGUI *g = &p->gui;
		g->initSubscriptions();
	}
}

//void DistViewController::setGUIEnabled(bool enable, TaskType tt)
//{
//	foreach (payload *p, map) {
//		p->gui.setEnabled(enable
//						  || tt == TT_BIN_COUNT || tt == TT_TOGGLE_VIEWER);
//	}
//}

sets_ptr DistViewController::subImage(representation::t type,
							   const std::vector<cv::Rect> &regions,
							   cv::Rect roi)
{
	GGDBG_CALL();
	return payloadMap[type]->model.subImage(regions, roi);
}

void DistViewController::addImage(representation::t type, sets_ptr temp,
							   const std::vector<cv::Rect> &regions,
							   cv::Rect roi)
{
	GGDBG_CALL();
	payloadMap[type]->model.addImage(temp, regions, roi);
}

//void DistViewController::setImage(representation::t type, SharedMultiImgPtr image,
//							   cv::Rect roi)
//{
//	int bins = map[type]->gui.getBinCount();
//	map[type]->model.setImage(image, roi, bins);
//}

void DistViewController::setActiveViewer(representation::t type)
{
	activeView = type;
	QMap<representation::t, payload*>::const_iterator it;
	for (it = payloadMap.begin(); it != payloadMap.end(); ++it) {
		 if (it.key() != activeView)
			it.value()->gui.setInactive();
	}
}

void DistViewController::pixelOverlay(int y, int x)
{

	if (y < 0 || x < 0) { // overlay became invalid
		emit pixelOverlayInvalid();
		return;
	}

	foreach (payload *p, payloadMap) {
		// not visible -> not subscribed -> no data
		if (p->gui.isVisible()) {
			QPolygonF overlay = p->model.getPixelOverlay(y, x);
			if (!overlay.empty())
				p->gui.insertPixelOverlay(overlay);
		}
	}
}

void DistViewController::processROIChage(cv::Rect roi)
{
	curROI = roi;
}

void DistViewController::processImageUpdate(representation::t repr,
											SharedMultiImgPtr image,
											bool duplicate)
{

	if (cv::Rect() == curROI) {
		std::cerr << "DistViewController::processImageUpdate() error: "
				  << "DVC internal ROI is empty" << std::endl;
	}
	if (duplicate) {
		GGDBGM(repr  << " is re-spawn" << endl);
	}
	if (distviewNeedsBinning[repr]) {
		distviewNeedsBinning[repr] = false;
		GGDBGM("following distview " << repr <<
			   " request for new binning" << endl);
		updateBinning(repr, image);
	} else {
		GGDBGP("ignoring" << endl)
	}
}

void DistViewController::processDistviewNeedsBinning(representation::t repr)
{
	GGDBGM(repr << " needs binning" << endl);
	distviewNeedsBinning[repr] = true;
}

void DistViewController::processPreROISpawn(const cv::Rect &oldroi,
											const cv::Rect &newroi,
											const std::vector<cv::Rect> &sub,
											const std::vector<cv::Rect> &add,
											bool profitable)
{
	// recycle existing distview payload
	roiSets = boost::shared_ptr<QMap<representation::t, sets_ptr> > (
				new QMap<representation::t, sets_ptr>() );
	if (profitable) {
		GGDBGM("INCREMENTAL distview update" << endl);
		foreach (representation::t repr, representation::all()) {
			if (isSubscribed(repr, distviewSubs->repr)) {
				GGDBGM("   BEGIN " << repr <<" distview update" << endl);
				(*roiSets)[repr] = subImage(repr, sub, newroi);
				GGDBGM("   END " << repr <<" distview update" << endl);
			}
		}
	} else {
		GGDBGM("FULL distview update" << endl);
		// nothing to do, distview payload will be reset in processPostROISpawn
	}
}

void DistViewController::processPostROISpawn(const cv::Rect &oldroi,
											 const cv::Rect &newroi,
											 const std::vector<cv::Rect> &sub,
											 const std::vector<cv::Rect> &add,
											 bool profitable)
{
	if (profitable && ! roiSets) {
		std::cerr << "DistViewController::processPostROISpawn error: "
					 "profitable && ! m_roiSets)" << std::endl;
	}
	if (profitable) {
		GGDBGM("INCREMENTAL distview update" << endl);
	} else {
		GGDBGM("FULL distview update" << endl);
	}
	foreach (representation::t repr, representation::all()) {
		if (isSubscribed(repr, distviewSubs->repr)) {
			if (profitable && roiSets) {
				addImage(repr, (*roiSets)[repr], add, newroi);
			} else {
				updateBinning(repr, im->getImage(repr));
			}
		}
	}
	// free sets map
	roiSets = boost::shared_ptr<QMap<representation::t, sets_ptr> > ();
}

void DistViewController::processDistviewSubscribeRepresentation(
		QObject *subscriber,
		representation::t repr)
{
	subscribe(subscriber, repr, distviewSubs->repr);
}

void DistViewController::processDistviewUnsubscribeRepresentation(
		QObject *subscriber,
		representation::t repr)
{
	unsubscribe(subscriber, repr, distviewSubs->repr);
}

void DistViewController::updateBinning(representation::t repr,
									   SharedMultiImgPtr image)
{
	int bins = payloadMap[repr]->gui.getBinCount();
	payloadMap[repr]->model.setImage(image, curROI, bins);
}

void DistViewController::changeBinCount(representation::t type, int bins)
{
	// TODO: might be called on initialization, which might be nasty
	// TODO: too harsh
	//queue->cancelTasks();
//	ctrl->disableGUI(TT_BIN_COUNT);

	payloadMap[type]->model.updateBinning(bins);

//	ctrl->enableGUILater();
}

void DistViewController::updateLabels(const cv::Mat1s& labels,
									  const QVector<QColor> &colors,
									  bool colorsChanged)
{
	if (!colors.empty()) {
		foreach (payload *p, payloadMap) {
			p->model.setLabelColors(colors);
			p->gui.updateLabelColors(colors);
		}
	}

	// binset update is needed if labels or colors changed
	if (labels.empty() && (!colorsChanged))
		return;

//	ctrl->disableGUI();

	foreach (payload *p, payloadMap) {
		p->model.updateLabels(labels, colors);
	}

//	ctrl->enableGUILater();
}

void DistViewController::updateLabelsPartially(const cv::Mat1s &labels,
											const cv::Mat1b &mask)
{
	/* test: is it worth it to do it incrementally
	 * (2 updates for each positive entry)
	 */
	bool profitable = (size_t(2 * cv::countNonZero(mask)) < mask.total());
	if (profitable) {

//		ctrl->disableGUI();

		foreach (payload *p, payloadMap) {
			p->model.updateLabelsPartially(labels, mask);
		}

//		ctrl->enableGUILater();
	} else {
		// just update the whole thing
		updateLabels(labels);
	}
}

void DistViewController::processNewBinning(representation::t type)
{
	payloadMap[type]->gui.rebuild();
}

void DistViewController::processNewBinningRange(representation::t type)
{
	DistViewModel &m = payloadMap[type]->model;
	m.clearMask();
	std::pair<multi_img::Value, multi_img::Value> range = m.getRange();
	payloadMap[type]->gui.setTitle(type, range.first, range.second);

	processNewBinning(type);
}

void DistViewController::propagateBandSelection(int band)
{
	emit bandSelected(activeView, band);
}

void DistViewController::drawOverlay(int band, int bin)
{
	DistViewModel &m = payloadMap[activeView]->model;
	m.fillMaskSingle(band, bin);
	emit requestOverlay(m.getHighlightMask());
}

void DistViewController::drawOverlay(const std::vector<std::pair<int, int> >& l,
									 int dim)
{
	DistViewModel &m = payloadMap[activeView]->model;
	if (dim > -1)
		m.updateMaskLimiters(l, dim);
	else
		m.fillMaskLimiters(l);

	emit requestOverlay(m.getHighlightMask());
}

void DistViewController::finishNormRangeImgChange(bool success)
{
	/* TODO: this is *so* wrong. what this does is after changing the range
	 * ensure that band cache is deleted and new band is shown
	 * however bandSelected will select this representation, even if it was not
	 * selected before. also this should be done by the image model
	 * and the image model has other means of doing this (empty the map)
	 */
/*	if (success) {
		SharedDataLock hlock((*image)->mutex);
		(*bands)[representation::IMG].assign((**image)->size(), NULL);
		hlock.unlock();
		emit bandSelected(representation::IMG, map.value(representation::IMG)->getSelection());
	}*/
}

void DistViewController::finishNormRangeGradChange(bool success)
{
	// ************** see above
/*	if (success) {
		SharedDataLock hlock((*gradient)->mutex);
		(*bands)[GRAD].assign((**gradient)->size(), NULL);
		hlock.unlock();
		emit bandSelected(GRAD,	map.value(GRAD)->getSelection());
	}*/
}

void DistViewController::toggleIgnoreLabels(bool toggle)
{
	// TODO: cancel previous toggleignorelabel tasks here!
//	ctrl->disableGUI(TT_TOGGLE_LABELS);

	foreach (payload *p, payloadMap) {
		p->model.toggleLabels(toggle);
	}

//	ctrl->enableGUILater();
}

void DistViewController::addHighlightToLabel()
{
	cv::Mat1b mask = payloadMap[activeView]->model.getHighlightMask();
	emit alterLabelRequested(currentLabel, mask, false);
}

void DistViewController::remHighlightFromLabel()
{
	cv::Mat1b mask = payloadMap[activeView]->model.getHighlightMask();
	emit alterLabelRequested(currentLabel, mask, true);
}
