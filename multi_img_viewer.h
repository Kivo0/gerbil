#ifndef MULTI_IMG_VIEWER_H
#define MULTI_IMG_VIEWER_H

#include "ui_multi_img_viewer.h"
#include "viewport.h"
#include "multi_img.h"

#include <vector>

class multi_img_viewer : public QWidget, private Ui::multi_img_viewer {
    Q_OBJECT
public:
	multi_img_viewer(QWidget *parent = 0);

	const QWidget* getViewport() { return viewport; }

	const QImage *labels;
	const QVector<QColor> *labelcolors;
public slots:
	void rebuild(int bins = 0);
	void setImage(const multi_img &image, bool gradient = false);
	void showLabeled(bool yes);
	void showUnLabeled(bool yes);

protected:
    void changeEvent(QEvent *e);
	void createBins(int bins);

	const multi_img *image;
};

#endif // MULTI_IMG_VIEWER_H
