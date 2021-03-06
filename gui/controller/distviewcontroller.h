#ifndef DISTVIEWCONTROLLER_H
#define DISTVIEWCONTROLLER_H

#include <controller/controller.h>
#include <dist_view/distviewmodel.h>
#include <dist_view/distviewgui.h>
#include <background_task/background_task_queue.h>

#include <QObject>
#include <QVector>
#include <QMap>

class DistViewController : public QObject
{
	Q_OBJECT

	struct Payload {
		Payload(representation::t type) : model(type), gui(type) {}

		DistViewModel model;
		DistViewGUI gui;
		bool binningNeeded;
		bool viewFolded; // Folding state, true -> distview folded (inactive).
		bool isSubscribed; // TODO: redundant state! Better remove payload on unsubscribe?
		sets_ptr tmp_binset; // used for recycling in ROI spawn
	};

public:
	explicit DistViewController(Controller *ctrl,
	                            BackgroundTaskQueue *taskQueue,
	                            ImageModel *im);

	void init();
	/** Initialize subscriptions.
	 *
	 * This needs to be called when all subscription signals/slots
	 * have been connected.
	 */
	void initSubscriptions();

	sets_ptr subImage(representation::t type,
	                  const std::vector<cv::Rect> &regions, cv::Rect roi);
	void addImage(representation::t type, sets_ptr temp,
	              const std::vector<cv::Rect> &regions, cv::Rect roi);

	// Old, we now listen to imageUpdate from ImageModel
	//void setImage(representation::t type, SharedMultiImgPtr image, cv::Rect roi);

public slots:
	void setActiveViewer(representation::t type);

	// rebinning in model needs to be displayed by viewport
	void processNewBinning(representation::t type);
	// rebinning in model done, also changed the range
	void processNewBinningRange(representation::t type);

	void finishNormRangeImgChange(bool success);
	void finishNormRangeGradChange(bool success);

	void toggleIgnoreLabels(bool toggle);

	void updateLabels(const cv::Mat1s &labels,
	                  const QVector<QColor>& colors = QVector<QColor>(),
	                  bool colorsChanged = false);
	void updateLabelsPartially(const cv::Mat1s &labels, const cv::Mat1b &mask);

	void changeBinCount(representation::t type, int bins);

	void propagateBandSelection(int band);

	void drawOverlay(int band, int bin);
	void drawOverlay(const std::vector<std::pair<int, int> > &l, int dim);

	// retrieve highlight mask and delegate alter-label request
	void addHighlightToLabel();
	void remHighlightFromLabel();
	// needed for the functions above
	void setCurrentLabel(int index) { currentLabel = index; }

	void pixelOverlay(int y, int x);

	void processROIChage(cv::Rect roi);
	void processImageUpdate(representation::t repr,
	                        SharedMultiImgPtr image,
	                        bool duplicate);

	void processDistviewNeedsBinning(representation::t repr);

	void processPreROISpawn(cv::Rect const & oldroi,
	                        cv::Rect const & newroi,
	                        std::vector<cv::Rect> const & sub,
	                        std::vector<cv::Rect> const & add,
	                        bool profitable
	                        );

	void processPostROISpawn(cv::Rect const & oldroi,
	                         cv::Rect const & newroi,
	                         std::vector<cv::Rect> const & sub,
	                         std::vector<cv::Rect> const & add,
	                         bool profitable
	                         );

	// folding state of a distview changed
	void processFoldingStateChanged(representation::t repr, bool folded);

	// save folding state of views
	void processLastWindowClosed();

	// pass on as subscriptions is not a qobject
	void subscribeRepresentation(QObject* o,representation::t r) {
		payloadMap[r]->isSubscribed = true;
	}
	// pass on as subscriptions is not a qobject
	void unsubscribeRepresentation(QObject* o,representation::t r) {
		payloadMap[r]->isSubscribed = false;
	}

signals:
	void toggleLabeled(bool);
	void toggleUnlabeled(bool);
	void labelSelected(int);

	void viewportAddSelection();
	void viewportRemSelection();

	void pixelOverlayInvalid();

	void normTargetChanged(bool useCurrent);
	void requestOverlay(const cv::Mat1b &mask);

	// a band was selected and should be shown in spatial view
	void bandSelected(representation::t type, int band);

	// add/delete current highlight to a label
	void alterLabelRequested(short,cv::Mat1b,bool);

	// viewport on-the-fly illuminant-correction (only IMG distview)
	void newIlluminantCurve(QVector<multi_img::Value>);
	void toggleIlluminationShown(bool show);

	// distviewmodel/viewport recognition of applied illuminant (only IMG)
	void newIlluminantApplied(QVector<multi_img::Value>);

protected:
	void updateBinning(representation::t repr, SharedMultiImgPtr image);

	QMap<representation::t, Payload*> payloadMap;
	Controller *ctrl;
	ImageModel *im;

	representation::t activeView;

	// needed for add/rem to/from label functionality
	int currentLabel;

	// the current ROI
	cv::Rect curROI;
};

#endif // DISTVIEWCONTROLLER_H
