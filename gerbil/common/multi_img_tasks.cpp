/*	
	Copyright(c) 2012 Petr Koupy <petr.koupy@gmail.com>

	This file may be licensed under the terms of of the GNU General Public
	License, version 3, as published by the Free Software Foundation. You can
	find it here: http://www.gnu.org/licenses/gpl.html
*/

#include "multi_img_tasks.h"

namespace CIEObserver {
	extern float x[];
	extern float y[];
	extern float z[];
}

namespace MultiImg {

namespace CommonTbb {

void CommonTbb::RebuildPixels::operator()(const tbb::blocked_range<size_t> &r) const
{
	for (size_t d = r.begin(); d != r.end(); ++d) {
		multi_img::Band &src = multi.bands[d];
		multi_img::BandConstIt it; size_t i;
		for (it = src.begin(), i = 0; it != src.end(); ++it, ++i)
			multi.pixels[i][d] = *it;
	}
}

void CommonTbb::ApplyCache::operator()(const tbb::blocked_range<size_t> &r) const
{
	for (size_t d = r.begin(); d != r.end(); ++d) {
		multi_img::Band &dst = multi.bands[d];
		multi_img::BandIt it; size_t i;
		for (it = dst.begin(), i = 0; it != dst.end(); ++it, ++i)
			*it = multi.pixels[i][d];
	}
}

}

void BgrSerial::run() 
{
	SharedDataRead rlock(multi->lock);
	cv::Mat_<cv::Vec3f> *newBgr = new cv::Mat_<cv::Vec3f>();
	*newBgr = (*multi)->bgr(); 
	SharedDataWrite wlock(bgr->lock);
	delete bgr->swap(newBgr);
}

void BgrTbb::run() 
{
	SharedDataRead rlock(multi->lock);

	cv::Mat_<cv::Vec3f> xyz((*multi)->height, (*multi)->width, 0.);
	float greensum = 0.f;
	for (size_t i = 0; i < (*multi)->size(); ++i) {
		int idx = ((int)((*multi)->meta[i].center + 0.5f) - 360) / 5;
		if (idx < 0 || idx > 94)
			continue;

		Xyz computeXyz(multi, xyz, i, idx);
		tbb::parallel_for(tbb::blocked_range2d<int>(0, xyz.rows, 0, xyz.cols), 
			computeXyz, tbb::auto_partitioner(), stopper);

		greensum += CIEObserver::y[idx];
	}

	if (greensum == 0.f)
		greensum = 1.f;

	cv::Mat_<cv::Vec3f> *newBgr = new cv::Mat_<cv::Vec3f>((*multi)->height, (*multi)->width);
	Bgr computeBgr(multi, xyz, *newBgr, greensum);
	tbb::parallel_for(tbb::blocked_range2d<int>(0, newBgr->rows, 0, newBgr->cols), 
		computeBgr, tbb::auto_partitioner(), stopper);

	if (stopper.is_group_execution_cancelled()) {
		stopper.reset();
		delete newBgr;
		return;
	} else {
		SharedDataWrite wlock(bgr->lock);
		delete bgr->swap(newBgr);
	}
}

void BgrTbb::Xyz::operator()(const tbb::blocked_range2d<int> &r) const
{
	for (int i = r.rows().begin(); i != r.rows().end(); ++i) {
		for (int j = r.cols().begin(); j != r.cols().end(); ++j) {
			cv::Vec3f &v = xyz(i, j);
			float intensity = (*multi)->bands[band](i, j) * 1.f/(*multi)->maxval;
			v[0] += CIEObserver::x[cie] * intensity;
			v[1] += CIEObserver::y[cie] * intensity;
			v[2] += CIEObserver::z[cie] * intensity;
		}
	}
}

void BgrTbb::Bgr::operator()(const tbb::blocked_range2d<int> &r) const
{
	for (int i = r.rows().begin(); i != r.rows().end(); ++i) {
		for (int j = r.cols().begin(); j != r.cols().end(); ++j) {
			cv::Vec3f &vs = xyz(i, j);
			cv::Vec3f &vd = bgr(i, j);
			for (unsigned int j = 0; j < 3; ++j)
				vs[j] /= greensum;
			(*multi)->xyz2bgr(vs, vd);
		}
	}
}

void GradientTbb::run() 
{
	SharedDataRead srcReadLock(source->lock);
	SharedDataRead curReadLock(current->lock);

	cv::Rect copyGlob = (*source)->roi & (*current)->roi;
	cv::Rect copySrc(0, 0, 0, 0);
	cv::Rect copyCur(0, 0, 0, 0);
	if (copyGlob.width > 0 && copyGlob.height > 0) {
		copySrc.x = copyGlob.x - (*source)->roi.x;
		copySrc.y = copyGlob.y - (*source)->roi.y;
		copySrc.width = copyGlob.width;
		copySrc.height = copyGlob.height;

		copyCur.x = copyGlob.x - (*current)->roi.x;
		copyCur.y = copyGlob.y - (*current)->roi.y;
		copyCur.width = copyGlob.width;
		copyCur.height = copyGlob.height;
	}

	std::vector<cv::Rect> calc;
	calc.push_back(cv::Rect(0, 0, copySrc.x, copySrc.y));
	calc.push_back(cv::Rect(copySrc.x, 0, copySrc.width, copySrc.y));
	calc.push_back(cv::Rect(copySrc.x + copySrc.width, 0, 
		(*source)->width - copySrc.width - copySrc.x, copySrc.y));
	calc.push_back(cv::Rect(0, copySrc.y, copySrc.x, copySrc.height));
	calc.push_back(cv::Rect(copySrc.x + copySrc.width, copySrc.y, 
		(*source)->width - copySrc.width - copySrc.x, copySrc.height));
	calc.push_back(cv::Rect(0, copySrc.y + copySrc.height, 
		copySrc.x, (*source)->height - copySrc.height - copySrc.y));
	calc.push_back(cv::Rect(copySrc.x, copySrc.y + copySrc.height, 
		copySrc.width, (*source)->height - copySrc.height - copySrc.y));
	calc.push_back(cv::Rect(copySrc.x + copySrc.width, copySrc.y + copySrc.height, 
		(*source)->width - copySrc.width - copySrc.x, 
		(*source)->height - copySrc.height - copySrc.y));

	multi_img *target = new multi_img(
		(*source)->height, (*source)->width, (*source)->size() - 1);
	if (copyGlob.width > 0 && copyGlob.height > 0) {
		for (size_t i = 0; i < target->size(); ++i) {
			multi_img::Band curBand = (*current)->bands[i](copyCur);
			multi_img::Band tgtBand = target->bands[i](copySrc);
			curBand.copyTo(tgtBand);
		}
	}

	curReadLock.unlock();

	multi_img temp((*source)->height, (*source)->width, (*source)->size());
	for (std::vector<cv::Rect>::iterator it = calc.begin(); it != calc.end(); ++it) {
		if (it->width > 0 && it->height > 0) {
			multi_img srcScope(**source, *it);
			multi_img tmpScope(temp, *it);
			Log computeLog(srcScope, tmpScope);
			tbb::parallel_for(tbb::blocked_range<size_t>(0, temp.size()), 
				computeLog, tbb::auto_partitioner(), stopper);
		}
	}
	temp.minval = 0.;
	temp.maxval = log((*source)->maxval);

	srcReadLock.unlock();

	for (std::vector<cv::Rect>::iterator it = calc.begin(); it != calc.end(); ++it) {
		if (it->width > 0 && it->height > 0) {
			multi_img tmpScope(temp, *it);
			multi_img tgtScope(*target, *it);
			Grad computeGrad(tmpScope, tgtScope);
			tbb::parallel_for(tbb::blocked_range<size_t>(0, target->size()), 
				computeGrad, tbb::auto_partitioner(), stopper);
		}
	}
	target->minval = -temp.maxval;
	target->maxval = temp.maxval;

	target->resetPixels();

	if (stopper.is_group_execution_cancelled()) {
		stopper.reset();
		delete target;
		return;
	} else {
		SharedDataWrite wlock(current->lock);
		delete current->swap(target);
	}
}

void GradientTbb::Log::operator()(const tbb::blocked_range<size_t> &r) const
{
	for (size_t i = r.begin(); i != r.end(); ++i) {
		cv::log(source.bands[i], target.bands[i]);
		//target.bands[i] = cv::max(target.bands[i], 0.);
		cv::max(target.bands[i], 0., target.bands[i]);
	}
}

void GradientTbb::Grad::operator()(const tbb::blocked_range<size_t> &r) const
{
	for (size_t i = r.begin(); i != r.end(); ++i) {
		//target.bands[i] = source.bands[i + 1] - source.bands[i];
		cv::subtract(source.bands[i + 1], source.bands[i], target.bands[i]);
		if (!source.meta[i].empty && !source.meta[i + 1].empty) {
			target.meta[i] = multi_img::BandDesc(
				source.meta[i].center, source.meta[i + 1].center);
		}
	}
}

void PcaTbb::run() 
{
	SharedDataRead rlock(source->lock);

	cv::Mat_<multi_img::Value> pixels(
		(*source)->size(), (*source)->width * (*source)->height);
	Pixels computePixels(**source, pixels);
	tbb::parallel_for(tbb::blocked_range<size_t>(0, (*source)->size()), 
		computePixels, tbb::auto_partitioner(), stopper);

	cv::PCA pca(pixels, cv::noArray(), CV_PCA_DATA_AS_COL, components);

	multi_img *result = new multi_img(
		(*source)->width, (*source)->height, pca.eigenvectors.rows);
	Projection computeProjection(pixels, *result, pca);
	tbb::parallel_for(tbb::blocked_range<size_t>(0, result->pixels.size()), 
		computeProjection, tbb::auto_partitioner(), stopper);

	CommonTbb::ApplyCache applyCache(*result);
	tbb::parallel_for(tbb::blocked_range<size_t>(0, result->size()), 
		applyCache, tbb::auto_partitioner(), stopper);

	result->dirty.setTo(0);
	result->anydirt = false;
	std::pair<multi_img::Value, multi_img::Value> range = result->data_range();
	result->minval = range.first;
	result->maxval = range.second;

	if (stopper.is_group_execution_cancelled()) {
		stopper.reset();
		delete result;
		return;
	} else {
		SharedDataWrite wlock(current->lock);
		delete current->swap(result);
	}
}

void PcaTbb::Pixels::operator()(const tbb::blocked_range<size_t> &r) const
{
	for (size_t d = r.begin(); d != r.end(); ++d) {
		multi_img::Band &src = source.bands[d];
		multi_img::BandConstIt it; size_t i;
		for (it = src.begin(), i = 0; it != src.end(); ++it, ++i)
			target(d, i) = *it;
	}
}

void PcaTbb::Projection::operator()(const tbb::blocked_range<size_t> &r) const
{
	for (size_t i = r.begin(); i != r.end(); ++i) {
		cv::Mat_<multi_img::Value> input = source.col(i);
		cv::Mat_<multi_img::Value> output(target.pixels[i], false);
		pca.project(input, output);
	}
}

}