/*
*   Single Point-of-view Projector Calibration System
*	Copyright (C) 2023 Damian Ziaber
*
*	This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
*
*	This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License along with this program. If not, see https://www.gnu.org/licenses/.
*/
#include "pch.h"
#include "GrayCodeCalibration.h"

using namespace std;

void GrayCodeCalibration::process(vector<cv::Mat> frames, cv::Mat& map1, cv::Mat& map2, cv::Size projectionSize, cv::Mat initial_homography, int meshRefinementCount, int meshRefinementDistLimit) {
	//Converting frames to Gray images
	vector<cv::Mat> processed_frames;
	processed_frames.reserve(frames.size());
	for (int i = 2; i < frames.size(); i++) {
		cv::Mat frame;
		cv::cvtColor(frames[i], frame, cv::COLOR_BGR2GRAY);
		processed_frames.push_back(frame);
	}

	//Getting light mask for the projection
	const int camera_height = processed_frames[0].size[0];
	const int camera_width = processed_frames[0].size[1];
	cv::Mat mask = getMask(frames[0], frames[1]);

	//Getting camera-projection pairs
	vector<cv::Point> cameraPoints, projectionPoints;
	getCameraProjectionPairs(cameraPoints, projectionPoints, processed_frames, projectionSize, mask);

	for (int i = 0; i < processed_frames.size(); i++)
		processed_frames[i].release();
	mask.release();

	DelaunayBasedCorrection::findMaps(cameraPoints, projectionPoints, map1, map2, projectionSize, initial_homography, meshRefinementCount, meshRefinementDistLimit);
}

cv::Mat GrayCodeCalibration::getMask(const cv::Mat whiteFrame, const cv::Mat blackFrame, const bool useOtsu, const int threshold) {
	cv::Mat difference;
	cv::Mat mask;
	cv::Mat processedWhiteFrame;
	cv::Mat processedBlackFrame;

	cv::cvtColor(blackFrame, processedBlackFrame, cv::COLOR_BGR2GRAY);
	cv::cvtColor(whiteFrame, processedWhiteFrame, cv::COLOR_BGR2GRAY);
	cv::subtract(processedWhiteFrame, processedBlackFrame, difference);
	if (useOtsu) {
		cv::GaussianBlur(difference, difference, cv::Size(5, 5), 0);
		cv::threshold(difference, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
	}
	else
		cv::threshold(difference, mask, threshold, 255, cv::THRESH_BINARY);
	
	difference.release();
	processedWhiteFrame.release();
	processedBlackFrame.release();

	return mask;
}


void GrayCodeCalibration::getCameraProjectionPairs(vector<cv::Point>& cameraPoints, vector<cv::Point>& projectionPoints, const vector<cv::Mat> frames, const cv::Size projectionSize, const cv::Mat mask, const int minPointCount, const int threshold) {
	const uint16_t column_frame_count = (uint16_t)ceil(log(double(projectionSize.width)) / log(2.0)) * 2;
	const uint32_t camera_height = frames[0].size[0];
	const uint32_t camera_width = frames[0].size[1];
	const uint32_t all_pixels = camera_width * camera_height;

	uint16_t* error = (uint16_t*)calloc(all_pixels, sizeof(uint16_t));
	uint16_t* coordinates = (uint16_t*)calloc(all_pixels, 2 * sizeof(uint16_t));
	bool* temp = (bool*)calloc(all_pixels, sizeof(bool));
	cv::Mat diff;

	for (int i = 0; i < column_frame_count; i += 2) {
		const uint8_t current_error = (column_frame_count - i) >> 1;
		cv::subtract(frames[i], frames[i + 1], diff, mask, CV_16S);
		diff.forEach<int16_t>([coordinates, temp, error, current_error, camera_width, threshold](int16_t& pixel, const int* position) {
			const int pos = (position[0] * camera_width + position[1]);
		coordinates[2 * pos] <<= 1;
		temp[pos] = temp[pos] != (pixel >= 0);
		if (pixel > -threshold && pixel < threshold && current_error > error[pos])
			error[pos] = current_error;
		if (temp[pos])
			coordinates[2 * pos]++;
			});
	}
	memset(temp, 0, all_pixels * sizeof(bool));

	for (uint16_t i = column_frame_count; i < frames.size(); i += 2) {
		const uint16_t current_error = ((uint16_t)frames.size() - column_frame_count - i) >> 1;
		cv::subtract(frames[i], frames[i + 1], diff, mask, CV_16S);
		diff.forEach<int16_t>([coordinates, temp, error, current_error, camera_width, threshold](int16_t& pixel, const int* position) {
			const int pos = (position[0] * camera_width + position[1]);
		coordinates[2 * pos + 1] <<= 1;
		temp[pos] = temp[pos] != (pixel >= 0);
		if (pixel > -threshold && pixel < threshold && current_error > error[pos])
			error[pos] = current_error;
		if (temp[pos])
			coordinates[2 * pos + 1]++;
			});
	}

	int* error_counts = (int*)calloc(5, sizeof(int));

	// Calculating allowed error to get minimum required points
	uint16_t* current_error = error;
	for (uint32_t i = 0; i < all_pixels; i++, current_error++) {
		if (*current_error < 4)
			error_counts[*current_error] ++;
	}
	int allowed_error = 0;
	for (; allowed_error < 4 && error_counts[allowed_error] < minPointCount; allowed_error++, error_counts[allowed_error] += error_counts[allowed_error - 1]);

	if (allowed_error < 4) {
		cameraPoints.reserve(error_counts[allowed_error]);
		projectionPoints.reserve(error_counts[allowed_error]);
	}
	free(error_counts);
	free(temp);

	uint16_t* current_point = coordinates;
	current_error = error;
	for (uint16_t y = 0; y < camera_height; y++) {
		for (uint16_t x = 0; x < camera_width; x++, current_point += 2, current_error++) {
			uint16_t p_x = *current_point;
			uint16_t p_y = *(current_point + 1);
			if (p_x >= 0 && p_x < projectionSize.width && p_y >= 0 && p_y < projectionSize.height && *current_error <= allowed_error) {
				cameraPoints.push_back(cv::Point(x, y));
				projectionPoints.push_back(cv::Point(p_x, p_y));
			}
		}
	}

	free(error);
	free(coordinates);
}
