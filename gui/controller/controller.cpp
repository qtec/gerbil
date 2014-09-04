#include "controller.h"
#include "subscriptions.h"
#include "controller/distviewcontroller.h"
#include <imginput.h>
#include <rectangles.h>

#include "model/imagemodel.h"
#include "model/labelingmodel.h"
#include "model/illuminationmodel.h"
#include "model/falsecolormodel.h"
#include "model/graphsegmentationmodel.h"
#include "model/clusteringmodel.h"

#include "widgets/mainwindow.h"

#define GGDBG_MODULE
#include "gerbil_gui_debug.h"

#include <boost/ref.hpp>
#include <cstdlib> // for exit()

// for DEBUG
#ifdef GGDBG_MODULE
std::ostream &operator<<(std::ostream& os, const cv::Rect& r)
{
	os << boost::format("%1%x%2%+%3%+%4%") % r.x % r.y % r.width % r.height;
	return os;
}
#endif // GGDBG

Controller::Controller(const std::string &filename, bool limited_mode,
	const QString &labelfile)
	// initialize all pointers so we don't access them too early w/o notice
	: im(0), lm(0), fm(0), illumm(0), gsm(0),
#ifdef WITH_SEG_MEANSHIFT
	  cm(0),
#endif
	  dvc(0),
	  queuethread(0),
	  subs(new Subscriptions)
{
	// start background task queue thread
	startQueue();

	im = new ImageModel(queue, limited_mode);
	// load image
	cv::Rect dimensions = im->loadImage(filename);
	if (dimensions.width < 1) {
		exit(4); // Qt's exit does not work before calling exec();
	}

	// create gui (perform initUI before connecting signals!)
	window = new MainWindow(limited_mode);
	window->initUI(filename);

	// initialize models
	initImage();
	fm = new FalseColorModel();
	initFalseColor(); // depends on ImageModel / initImage()

	// The order of connection is crucial for fm and Controller.
	// fm needs to get the signal first. Otherwise it will
	// hand out invalid cached data.
	connect(im, SIGNAL(imageUpdate(representation::t,SharedMultiImgPtr)),
			fm, SLOT(processImageUpdate(representation::t,SharedMultiImgPtr)));
	connect(im, SIGNAL(imageUpdate(representation::t,SharedMultiImgPtr)),
			this, SLOT(processImageUpdate(representation::t)));

	lm = new LabelingModel();
	initLabeling(dimensions);
	illumm = new IllumModel(&queue);
	initIlluminant();
	gsm = new GraphSegmentationModel(&queue);
	initGraphSegmentation(); // depends on ImageModel / initImage()
#ifdef WITH_SEG_MEANSHIFT
	cm = new ClusteringModel();
#endif /* WITH_SEG_MEANSHIFT */

	// initialize sub-controllers (after initializing the models...)
	dvc = new DistViewController(this, &queue);
	dvc->init();

	// init dock widgets
	initDocks();

	// connect slots/signals
	window->initSignals(this, dvc);

	/* TODO: better place. But do not use init model functions:
	 * dvc are created after these are called
	 */
	connect(dvc, SIGNAL(requestOverlay(cv::Mat1b)),
			this, SIGNAL(requestOverlay(cv::Mat1b)));
	connect(lm,
			SIGNAL(newLabeling(const cv::Mat1s&, const QVector<QColor>&, bool)),
			dvc, SLOT(updateLabels(cv::Mat1s,QVector<QColor>,bool)));
	connect(lm, SIGNAL(partialLabelUpdate(const cv::Mat1s&,const cv::Mat1b&)),
			dvc, SLOT(updateLabelsPartially(const cv::Mat1s&,const cv::Mat1b&)));
	connect(dvc, SIGNAL(alterLabelRequested(short,cv::Mat1b,bool)),
			lm, SLOT(alterLabel(short,cv::Mat1b,bool)));
	connect(illumm, SIGNAL(newIlluminantCurve(QVector<multi_img::Value>)),
			dvc, SIGNAL(newIlluminantCurve(QVector<multi_img::Value>)));
	connect(illumm, SIGNAL(newIlluminantApplied(QVector<multi_img::Value>)),
			dvc, SIGNAL(newIlluminantApplied(QVector<multi_img::Value>)));

	connect(im, SIGNAL(imageUpdate(representation::t,SharedMultiImgPtr)),
			cm, SLOT(processImageUpdate(representation::t,SharedMultiImgPtr)));

	/* start with initial label or provided labeling
	 * Do this after all signals are connected, and before initial ROI spawn!
	 */
	if (labelfile.isEmpty()) {
		lm->addLabel();
	} else {
		lm->loadLabeling(labelfile);
	}

	/* Initial ROI images spawning. Do it before showing the window but after
	 * all signals were connected! */
	//GGDBGM("dimensions " << dimensions << endl);
	m_roi = dimensions; // initial ROI is image size, except:
	if (m_roi.area() > 262144) {
		// image is bigger than 512x512, start with a smaller ROI in center
		m_roi.width = std::min(m_roi.width, 512);
		m_roi.height = std::min(m_roi.height, 512);
		m_roi.x = 0.5*(dimensions.width - m_roi.width);
		m_roi.y = 0.5*(dimensions.height - m_roi.height);
	}

	GGDBGM("roi " << m_roi  << endl);
	spawnROI();

	// The IMG representation must always be subscribed. Otherwise all the logic
	// in ImageModel fails. So we subscribe the Controller forever.
	processSubscribeRepresentation(this, representation::IMG);

	GGDBGM("init distview subscriptions" << endl);
	dvc->initSubscriptions();

	GGDBGM("init done, showing mainwindow" << endl);

	// we're done! show window
	window->show();
}

Controller::~Controller()
{
	delete window;

	delete dvc;

	delete im;
	delete lm;
	delete fm;
	delete illumm;
	delete gsm;
	delete cm;

	delete subs;

	// stop background task queue thread
	stopQueue();
}


/** Image management **/

// connect all signals between model and other parties
void Controller::initImage()
{
   // nothing
}

// depends on ImageModel
void Controller::initFalseColor()
{
	fm->setMultiImg(representation::IMG, im->getImage(representation::IMG));
	fm->setMultiImg(representation::GRAD, im->getImage(representation::GRAD));
}

void Controller::initIlluminant()
{
	illumm->setMultiImage(im->getFullImage());

	connect(illumm, SIGNAL(requestInvalidateROI(cv::Rect)),
			this, SLOT(invalidateROI(cv::Rect)));

	/* TODO: models should not request this! the controller of the model has
	 * to guard its operations!
	 */
//	connect(illumm, SIGNAL(setGUIEnabledRequested(bool,TaskType)),
//			this, SLOT(setGUIEnabled(bool, TaskType)), Qt::DirectConnection);
}

void Controller::initGraphSegmentation()
{
	gsm->setMultiImage(representation::IMG, im->getImage(representation::IMG));
	gsm->setMultiImage(representation::GRAD, im->getImage(representation::GRAD));

	connect(gsm, SIGNAL(alterLabelRequested(short,cv::Mat1b,bool)),
			lm, SLOT(alterLabel(short,cv::Mat1b,bool)));

//	connect(gsm, SIGNAL(setGUIEnabledRequested(bool,TaskType)),
//			this, SLOT(setGUIEnabled(bool, TaskType)));

	// (gsm seedingDone <-> bandDock seedingDone connection in initDocks)
}

/** Labeling management **/

// connect all signals between model and other parties
void Controller::initLabeling(cv::Rect dimensions)
{
	// initialize label model
	lm->setDimensions(dimensions.height, dimensions.width);
}

void Controller::spawnROI(cv::Rect roi)
{
	const bool reuse = true;
	updateROI(reuse, roi);
}

void Controller::invalidateROI(cv::Rect roi)
{
	const bool reuse = false;
	updateROI(reuse, roi);
}

void Controller::rescaleSpectrum(int bands)
{
	// TODO: this was cancelTasks with current ROI. Now all tasks cancelled.
	queue.cancelTasks();
//	disableGUI(TT_BAND_COUNT);

	updateROI(false, cv::Rect(), bands);

	//	enableGUILater();
}

void Controller::debugSubscriptions()
{
	//std::cerr << "** TYPE      subscribed flag" << std::endl;
	foreach (representation::t type, representation::all()) {
		std::cerr << "** " << std::left << std::setw(7) << type;
		if (haveSubscriber(type)) {
			 std::cerr << "    subscribed";
		} else {
			std::cerr <<  "not subscribed";
		}
		std::cerr << std::endl;
	}

}

void Controller::updateROI(bool reuse, cv::Rect roi, int bands)
{
	// no new ROI provided
	if (cv::Rect() == roi) {
		roi = m_roi ;
	} else {
		m_roi = roi;
	}
	GGDBGM("bands=" << bands << ", roi=" << roi << endl);

	//GGDBGM("no-reuse HACK active" << endl);
	//reuse = false;

	// prepare incremental update and test worthiness
	std::vector<cv::Rect> sub, add;
	if (reuse) {
		/* compute if it is profitable to add/sub pixels given old and new ROI,
		 * instead of full recomputation, and retrieve corresponding regions
		 */
		bool profitable = rectTransform(im->getROI(), roi, sub, add);
		if (!profitable)
			reuse = false;
	} else {
		// invalidate existing ROI information (to not re-use data)
		im->invalidateROI();
	}

	/** FIRST STEP: recycle existing payload **/
	QMap<representation::t, sets_ptr> sets;
	if (reuse) {
		foreach (representation::t type, representation::all()) {
			if (haveSubscriber(type)) {
				GGDBGM("     subscribed " << type <<
					   " -> incremental update" << endl);
				sets[type] = dvc->subImage(type, sub, roi);
			}
		}
	}

	/** SECOND STEP: update metadata */

	lm->updateROI(roi);
	illumm->setRoi(roi);

	/** THIRD STEP: update payload */
	/* this has to be done in the right order!
	 * IMG before all others, GRAD before GRADPCA
	 * it is implicit here but we would like this knowledge to be part of
	 * image model's logic
	 */
	foreach (representation::t type, representation::all()) {

		bool sub = haveSubscriber(type);

		if (sub) {
			GGDBGM("     subscribed " << type << endl);
		} else {
			GGDBGM("not  subscribed " << type << endl);
		}

		if (sub) {
			/* tasks to (incrementally) re-calculate image data */
			im->spawn(type, roi, bands);

			/* tasks to (incrementally) update distribution view */
			if (reuse) {
				dvc->addImage(type, sets[type], add, roi);
			} else {
				dvc->setImage(type, im->getImage(type), roi);
			}
		}
	}


	// TODO: better method to make sure values in normalizationDock are correct
	// that means as soon as we have these values, report them directly to the
	// GUI.
	/*if (type == GRAD) {
		emit normTargetChanged(true);
	}*/
}

bool Controller::haveSubscriber(representation::t type)
{
	foreach (Subscription<representation::t> const& sub, subs->repr) {
		if (sub.subsid == type) {
			return true;
		}
	}
	return false;
}

//void Controller::setGUIEnabled(bool enable, TaskType tt)
//{
//	/** for enable, this just re-enables everything
//	 * for disable, this typically disables everything except the sender, so
//	 * that the user can re-decide on that aspect or sth.
//	 * it is a bit strange
//	 */
//	window->setGUIEnabled(enable, tt);

//	// tell other controllers
//	dvc->setGUIEnabled(enable, tt);

///// DOCKS

//    setGUIEnabledDocks(enable, tt);
//}


/** Tasks and queue thread management */

//void Controller::enableGUILater()
//{
//    // TODO: old:
//    //	BackgroundTask *t = (withROI ? new BackgroundTask(im->getROI())
//    //								 : new BackgroundTask()
//    BackgroundTask *t = new BackgroundTask();
//	BackgroundTaskPtr taskEpilog(t);
//	QObject::connect(taskEpilog.get(), SIGNAL(finished(bool)),
//		this, SLOT(enableGUINow(bool)), Qt::QueuedConnection);
//	queue.push(taskEpilog);
//}

//void Controller::enableGUINow(bool forreal)
//{
//	if (forreal)
//		setGUIEnabled(true);
//}

//void Controller::disableGUI(TaskType tt)
//{
//	setGUIEnabled(false, tt);
//}

void Controller::processSubscribeImageBand(QObject *subscriber, representation::t repr, int bandId)
{
	assert(subs);
	// also subscribe to the relevant representation
	processSubscribeRepresentation(subscriber, repr);
	// if not inserted, the subscription already exists -> no need to update
	if (subscribe(subscriber, ImageBandId(repr, bandId), subs->imageBand)) {
		im->computeBand(repr, bandId);
	}
}

void Controller::processUnsubscribeImageBand(QObject *subscriber, representation::t repr, int bandId)
{
	assert(subs);
	subs->imageBand.erase(Subscription<ImageBandId>(subscriber, ImageBandId(repr, bandId)));
	processUnsubscribeRepresentation(subscriber, repr);
}

void Controller::processSubscribeFalseColor(QObject *subscriber, FalseColoring::Type coloring)
{
	//GGDBGM(coloring << endl);
	assert(subs);
	// also subscribe to the relevant representation
	processSubscribeRepresentation(subscriber, FalseColoring::basis(coloring));
	if (subscribe(subscriber, coloring, subs->falseColor)) {
		//GGDBGM("requesting from fm " << coloring << endl);
		fm->requestColoring(coloring);
	}
}

void Controller::processUnsubscribeFalseColor(QObject *subscriber, FalseColoring::Type coloring)
{
	//GGDBGM(coloring << endl);
	assert(subs);
	subs->falseColor.erase(Subscription<FalseColoring::Type>(subscriber, coloring));
	if (!isSubscribed(coloring, subs->falseColor)) {
		// no more subscriptions for coloring,
		// cancel computation if any.
		fm->cancelComputation(coloring);
	}
	processUnsubscribeRepresentation(subscriber, FalseColoring::basis(coloring));
}

void Controller::processRecalcFalseColor(FalseColoring::Type coloringType)
{
	assert(subs);
	if (isSubscribed(coloringType, subs->falseColor)) {
		fm->requestColoring(coloringType, /* recalc */ true);
	}
}

void Controller::processSubscribeRepresentation(QObject *subscriber, representation::t repr)
{
	assert(subs);
	if (subscribe(subscriber, repr, subs->repr)) {
		GGDBGM("new subscription, spawning ROI "<< m_roi << " for " << repr << endl);
		im->spawn(repr, m_roi, -1);
		dvc->setImage(repr, im->getImage(repr), m_roi);
	}
}

void Controller::processUnsubscribeRepresentation(QObject *subscriber, representation::t repr)
{
	assert(subs);
	GGDBGM("unsubscribe " << repr << endl);
	subs->repr.erase(Subscription<representation::t>(subscriber, repr));
}

void Controller::startQueue()
{
	// start worker thread
	//boost::ref h(queue);
	queuethread = new boost::thread(boost::ref(queue));
	// h is not needed anymore
}

void Controller::stopQueue()
{
	// cancel all jobs, then wait for thread to return
	queue.halt();
	queuethread->join();
	delete queuethread;
	queuethread = 0;
}

// method for debugging focus
void Controller::focusChange(QWidget *old, QWidget *now)
{
	if (!old || !now)
		return;
	std::cerr << "Focus changed from " << old->objectName().toStdString()
			  << " to " << now->objectName().toStdString() << std::endl;
}

void Controller::processImageUpdate(representation::t repr)
{
	//GGDBGM(repr << endl);
	// image band updates

	Subscription<ImageBandId>::IdTypeSet bandUpdates;

	assert(subs);
	foreach (Subscription<ImageBandId> const& sub, subs->imageBand) {
		if(repr == sub.subsid.repr)	 {
			bandUpdates.insert(sub.subsid);
		}
	}
	foreach (ImageBandId const& ib, bandUpdates) {
		//GGDBGM("requesting band " << ib.first << " " << ib.second << endl);
		im->computeBand(ib.repr, ib.bandx);
	}

	// false color updates

	typedef std::tr1::unordered_set<FalseColoring::Type, std::tr1::hash<int> > FalseColoringSet;
	FalseColoringSet fcUpdates;
	foreach (Subscription<FalseColoring::Type> const& sub, subs->falseColor) {
		FalseColoring::Type coloring = sub.subsid;
		if (FalseColoring::isBasedOn(coloring, repr)) {
			//GGDBGM("found subscriber for " << coloring << " based on " << repr << endl);
			fcUpdates.insert(coloring);
		}
	}
	foreach (FalseColoringSet::value_type const& coloring, fcUpdates) {
		//GGDBGM("requesting from fm " << coloring << endl);
		emit pendingFalseColorUpdate(coloring);
		fm->requestColoring(coloring);
	}
}
