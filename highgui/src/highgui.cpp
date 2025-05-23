//
// Copyright (C) 2021 nihui
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <string.h>
#include <fstream>
#include <sstream>
#include "exif.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#if __ARM_NEON
#define STBI_NEON
#endif
#if __riscv_vector
#define STBI_RVV
#endif
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#if CV_WITH_CVI
#include "jpeg_decoder_cvi.h"
#endif
#if CV_WITH_AW
#include "jpeg_decoder_aw.h"
#include "jpeg_encoder_aw.h"
#endif
#if CV_WITH_RK
#include "jpeg_encoder_rk_mpp.h"
#endif
#if CV_WITH_RPI
#include "jpeg_encoder_v4l_rpi.h"
#endif
#if CV_WITH_CIX
#include "jpeg_encoder_v4l_cix.h"
#endif
#if defined __linux__ && !__ANDROID__
#include "display_fb.h"
#endif

#ifdef _WIN32
#include "display_win32.h"
#include <thread>
#endif

namespace cv {
//
//     1        2       3      4         5            6           7          8
//
//   888888  888888      88  88      8888888888  88                  88  8888888888
//   88          88      88  88      88  88      88  88          88  88      88  88
//   8888      8888    8888  8888    88          8888888888  8888888888          88
//   88          88      88  88
//   88          88  888888  888888
//
// ref http://sylvana.net/jpegcrop/exif_orientation.html
static void rotate_by_orientation(const Mat& src, Mat& dst, int orientation)
{
    if (orientation == 1)
    {
        dst = src;
    }
    if (orientation == 2)
    {
        cv::flip(src, dst, 1);
    }
    if (orientation == 3)
    {
        cv::flip(src, dst, -1);
    }
    if (orientation == 4)
    {
        cv::flip(src, dst, 0);
    }
    if (orientation == 5)
    {
        cv::transpose(src, dst);
    }
    if (orientation == 6)
    {
        Mat tmp;
        cv::flip(src, tmp, 0);
        cv::transpose(tmp, dst);
    }
    if (orientation == 7)
    {
        Mat tmp;
        cv::flip(src, tmp, -1);
        cv::transpose(tmp, dst);
    }
    if (orientation == 8)
    {
        Mat tmp;
        cv::flip(src, tmp, 1);
        cv::transpose(tmp, dst);
    }
}

Mat imread(const String& filename, int flags)
{
    int desired_channels = 0;
    if (flags == IMREAD_UNCHANGED)
    {
        desired_channels = 0;
    }
    else if (flags == IMREAD_GRAYSCALE)
    {
        desired_channels = 1;
    }
    else if (flags == IMREAD_COLOR)
    {
        desired_channels = 3;
    }
    else
    {
        // unknown flags
        return Mat();
    }

    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp)
    {
        fprintf(stderr, "fopen %s failed\n", filename.c_str());
        return Mat();
    }

    std::vector<unsigned char> filedata;
    {
        fseek(fp, 0, SEEK_END);
        size_t len = ftell(fp);
        rewind(fp);
        filedata.resize(len);
        size_t nread = fread(filedata.data(), 1, len, fp);
        if (nread != len)
        {
            filedata.clear();
        }
    }

    fclose(fp);

    if (filedata.empty())
    {
        // empty file
        fprintf(stderr, "filedata empty\n");
        return Mat();
    }

    const unsigned char* buf_data = (const unsigned char*)filedata.data();
    size_t buf_size = filedata.size();

    if (buf_size > 4 && buf_data[0] == 0xFF && buf_data[1] == 0xD8)
    {
        // jpg magic
#if CV_WITH_AW
        if (jpeg_decoder_aw::supported(buf_data, buf_size))
        {
            int w = 0;
            int h = 0;
            int c = desired_channels;

            jpeg_decoder_aw d;
            int ret = d.init(buf_data, buf_size, &w, &h, &c);
            if (ret == 0 && (c == 1 || c == 3))
            {
                Mat img;
                if (c == 1)
                {
                    img.create(h, w, CV_8UC1);
                }
                else // if (c == 3)
                {
                    img.create(h, w, CV_8UC3);
                }

                ret = d.decode(buf_data, buf_size, img.data);
                if (ret == 0)
                {
                    d.deinit();
                    return img;
                }
            }

            // fallback to stbi_load_from_memory
        }
#endif
#if CV_WITH_CVI
        if (jpeg_decoder_cvi::supported(buf_data, buf_size))
        {
            int w = 0;
            int h = 0;
            int c = desired_channels;

            jpeg_decoder_cvi d;
            int ret = d.init(buf_data, buf_size, &w, &h, &c);
            if (ret == 0 && (c == 1 || c == 3))
            {
                Mat img;
                if (c == 1)
                {
                    img.create(h, w, CV_8UC1);
                }
                else // if (c == 3)
                {
                    img.create(h, w, CV_8UC3);
                }

                ret = d.decode(buf_data, buf_size, img.data);
                if (ret == 0)
                {
                    d.deinit();
                    return img;
                }
            }

            // fallback to stbi_load_from_memory
        }
#endif
    }

    int w;
    int h;
    int c;
    unsigned char* pixeldata = stbi_load_from_memory(buf_data, buf_size, &w, &h, &c, desired_channels);
    if (!pixeldata)
    {
        // load failed
        return Mat();
    }

    if (desired_channels)
    {
        c = desired_channels;
    }

    // copy pixeldata to Mat
    Mat img;
    if (c == 1)
    {
        img.create(h, w, CV_8UC1);
    }
    else if (c == 3)
    {
        img.create(h, w, CV_8UC3);
    }
    else if (c == 4)
    {
        img.create(h, w, CV_8UC4);
    }
    else
    {
        // unexpected channels
        stbi_image_free(pixeldata);
        return Mat();
    }

    memcpy(img.data, pixeldata, w * h * c);

    stbi_image_free(pixeldata);

    // resolve exif orientation
    {
        std::string s((const char*)buf_data, buf_size);
        std::istringstream iss(s);

        ExifReader exif_reader(iss);
        if (exif_reader.parse())
        {
            ExifEntry_t e = exif_reader.getTag(ORIENTATION);
            int orientation = e.field_u16;
            if (orientation >= 1 && orientation <= 8)
                rotate_by_orientation(img, img, orientation);
        }
    }

    // rgb to bgr
    if (c == 3)
    {
        cvtColor(img, img, COLOR_RGB2BGR);
    }
    if (c == 4)
    {
        cvtColor(img, img, COLOR_RGBA2BGRA);
    }

    return img;
}

bool imwrite(const String& filename, InputArray _img, const std::vector<int>& params)
{
    const char* _ext = strrchr(filename.c_str(), '.');
    if (!_ext)
    {
        // missing extension
        return false;
    }

    String ext = _ext;
    Mat img = _img.getMat();

    int c = 0;
    if (img.type() == CV_8UC1)
    {
        c = 1;
    }
    else if (img.type() == CV_8UC3)
    {
        c = 3;
    }
    else if (img.type() == CV_8UC4)
    {
        c = 4;
    }
    else
    {
        // unexpected image channels
        return false;
    }

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG")
    {
#if CV_WITH_AW
        if (jpeg_encoder_aw::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            jpeg_encoder_aw e;
            int ret = e.init(img.cols, img.rows, c, quality);
            if (ret == 0)
            {
                ret = e.encode(img.data, filename.c_str());
                if (ret == 0)
                {
                    e.deinit();
                    return true;
                }
            }

            // fallback to stb_image_write
        }
#endif
#if CV_WITH_RK
        if (jpeg_encoder_rk_mpp::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            jpeg_encoder_rk_mpp e;
            int ret = e.init(img.cols, img.rows, c, quality);
            if (ret == 0)
            {
                ret = e.encode(img.data, filename.c_str());
                if (ret == 0)
                {
                    e.deinit();
                    return true;
                }
            }

            // fallback to stb_image_write
        }
#endif
#if CV_WITH_RPI
        if (jpeg_encoder_v4l_rpi::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            // cache jpeg_encoder_v4l_rpi context
            static int old_w = 0;
            static int old_h = 0;
            static int old_ch = 0;
            static int old_quality = 0;
            static jpeg_encoder_v4l_rpi e;
            if (img.cols == old_w && img.rows == old_h && c == old_ch && quality == old_quality)
            {
                int ret = e.encode(img.data, filename.c_str());
                if (ret == 0)
                    return true;
            }
            else
            {
                int ret = e.init(img.cols, img.rows, c, quality);
                if (ret == 0)
                {
                    ret = e.encode(img.data, filename.c_str());
                    if (ret == 0)
                    {
                        old_w = img.cols;
                        old_h = img.rows;
                        old_ch = c;
                        old_quality = quality;
                        return true;
                    }
                }
            }
            // fallback to stb_image_write
        }
#endif
#if CV_WITH_CIX
        if (jpeg_encoder_v4l_cix::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            // cache jpeg_encoder_v4l_cix context
            static int old_w = 0;
            static int old_h = 0;
            static int old_ch = 0;
            static int old_quality = 0;
            static jpeg_encoder_v4l_cix e;
            if (img.cols == old_w && img.rows == old_h && c == old_ch && quality == old_quality)
            {
                int ret = e.encode(img.data, filename.c_str());
                if (ret == 0)
                    return true;
            }
            else
            {
                int ret = e.init(img.cols, img.rows, c, quality);
                if (ret == 0)
                {
                    ret = e.encode(img.data, filename.c_str());
                    if (ret == 0)
                    {
                        old_w = img.cols;
                        old_h = img.rows;
                        old_ch = c;
                        old_quality = quality;
                        return true;
                    }
                }
            }

            // fallback to stb_image_write
        }
#endif
    }

    // bgr to rgb
    if (c == 3)
    {
        Mat img2;
        cvtColor(img, img2, COLOR_BGR2RGB);
        img = img2;
    }
    if (c == 4)
    {
        Mat img2;
        cvtColor(img, img2, COLOR_BGRA2RGBA);
        img = img2;
    }

    if (!img.isContinuous())
    {
        img = img.clone();
    }

    bool success = false;

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG")
    {
        int quality = 95;
        for (size_t i = 0; i < params.size(); i += 2)
        {
            if (params[i] == IMWRITE_JPEG_QUALITY)
            {
                quality = params[i + 1];
                break;
            }
        }
        success = stbi_write_jpg(filename.c_str(), img.cols, img.rows, c, img.data, quality);
    }
    else if (ext == ".png" || ext == ".PNG")
    {
        success = stbi_write_png(filename.c_str(), img.cols, img.rows, c, img.data, 0);
    }
    else if (ext == ".bmp" || ext == ".BMP")
    {
        success = stbi_write_bmp(filename.c_str(), img.cols, img.rows, c, img.data);
    }
    else
    {
        // unknown extension type
        return false;
    }

    return success;
}

Mat imdecode(InputArray _buf, int flags)
{
    int desired_channels = 0;
    if (flags == IMREAD_UNCHANGED)
    {
        desired_channels = 0;
    }
    else if (flags == IMREAD_GRAYSCALE)
    {
        desired_channels = 1;
    }
    else if (flags == IMREAD_COLOR)
    {
        desired_channels = 3;
    }
    else
    {
        // unknown flags
        return Mat();
    }

    Mat buf = _buf.getMat();

    if (!buf.isContinuous())
    {
        buf = buf.clone();
    }

    const unsigned char* buf_data = (const unsigned char*)buf.data;
    size_t buf_size = buf.cols * buf.rows * buf.elemSize();

    if (buf_size > 4 && buf_data[0] == 0xFF && buf_data[1] == 0xD8)
    {
        // jpg magic
#if CV_WITH_AW
        if (jpeg_decoder_aw::supported(buf_data, buf_size))
        {
            int w = 0;
            int h = 0;
            int c = desired_channels;

            jpeg_decoder_aw d;
            int ret = d.init(buf_data, buf_size, &w, &h, &c);
            if (ret == 0 && (c == 1 || c == 3))
            {
                Mat img;
                if (c == 1)
                {
                    img.create(h, w, CV_8UC1);
                }
                else // if (c == 3)
                {
                    img.create(h, w, CV_8UC3);
                }

                ret = d.decode(buf_data, buf_size, img.data);
                if (ret == 0)
                {
                    d.deinit();
                    return img;
                }
            }

            // fallback to stbi_load_from_memory
        }
#endif
#if CV_WITH_CVI
        if (jpeg_decoder_cvi::supported(buf_data, buf_size))
        {
            int w = 0;
            int h = 0;
            int c = desired_channels;

            jpeg_decoder_cvi d;
            int ret = d.init(buf_data, buf_size, &w, &h, &c);
            if (ret == 0 && (c == 1 || c == 3))
            {
                Mat img;
                if (c == 1)
                {
                    img.create(h, w, CV_8UC1);
                }
                else // if (c == 3)
                {
                    img.create(h, w, CV_8UC3);
                }

                ret = d.decode(buf_data, buf_size, img.data);
                if (ret == 0)
                {
                    d.deinit();
                    return img;
                }
            }

            // fallback to stbi_load_from_memory
        }
#endif
    }

    int w;
    int h;
    int c;
    unsigned char* pixeldata = stbi_load_from_memory(buf_data, buf_size, &w, &h, &c, desired_channels);
    if (!pixeldata)
    {
        // load failed
        return Mat();
    }

    if (desired_channels)
    {
        c = desired_channels;
    }

    // copy pixeldata to Mat
    Mat img;
    if (c == 1)
    {
        img.create(h, w, CV_8UC1);
    }
    else if (c == 3)
    {
        img.create(h, w, CV_8UC3);
    }
    else if (c == 4)
    {
        img.create(h, w, CV_8UC4);
    }
    else
    {
        // unexpected channels
        stbi_image_free(pixeldata);
        return Mat();
    }

    memcpy(img.data, pixeldata, w * h * c);

    stbi_image_free(pixeldata);

    // resolve exif orientation
    {
        std::string s((const char*)buf_data, buf_size);
        std::istringstream iss(s);

        ExifReader exif_reader(iss);
        if (exif_reader.parse())
        {
            ExifEntry_t e = exif_reader.getTag(ORIENTATION);
            int orientation = e.field_u16;
            if (orientation >= 1 && orientation <= 8)
                rotate_by_orientation(img, img, orientation);
        }
    }

    // rgb to bgr
    if (c == 3)
    {
        cvtColor(img, img, COLOR_RGB2BGR);
    }
    if (c == 4)
    {
        cvtColor(img, img, COLOR_RGBA2BGRA);
    }

    return img;
}

static void imencode_write_func(void *context, void *data, int size)
{
    std::vector<uchar>* buf = (std::vector<uchar>*)context;
    buf->insert(buf->end(), (uchar*)data, (uchar*)data + size);
}

bool imencode(const String& ext, InputArray _img, std::vector<uchar>& buf, const std::vector<int>& params)
{
    Mat img = _img.getMat();

    int c = 0;
    if (img.type() == CV_8UC1)
    {
        c = 1;
    }
    else if (img.type() == CV_8UC3)
    {
        c = 3;
    }
    else if (img.type() == CV_8UC4)
    {
        c = 4;
    }
    else
    {
        // unexpected image channels
        return false;
    }

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG")
    {
#if CV_WITH_AW
        if (jpeg_encoder_aw::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            jpeg_encoder_aw e;
            int ret = e.init(img.cols, img.rows, c, quality);
            if (ret == 0)
            {
                ret = e.encode(img.data, buf);
                if (ret == 0)
                {
                    e.deinit();
                    return true;
                }
            }

            // fallback to stb_image_write
        }
#endif
#if CV_WITH_RK
        if (jpeg_encoder_rk_mpp::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            jpeg_encoder_rk_mpp e;
            int ret = e.init(img.cols, img.rows, c, quality);
            if (ret == 0)
            {
                ret = e.encode(img.data, buf);
                if (ret == 0)
                {
                    e.deinit();
                    return true;
                }
            }

            // fallback to stb_image_write
        }
#endif
#if CV_WITH_RPI
        if (jpeg_encoder_v4l_rpi::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            jpeg_encoder_v4l_rpi e;
            int ret = e.init(img.cols, img.rows, c, quality);
            if (ret == 0)
            {
                ret = e.encode(img.data, buf);
                if (ret == 0)
                {
                    e.deinit();
                    return true;
                }
            }
            // fallback to stb_image_write
        }
#endif
#if CV_WITH_CIX
        if (jpeg_encoder_v4l_cix::supported(img.cols, img.rows, c))
        {
            // anything to bgr
            if (!img.isContinuous())
            {
                img = img.clone();
            }

            int quality = 95;
            for (size_t i = 0; i < params.size(); i += 2)
            {
                if (params[i] == IMWRITE_JPEG_QUALITY)
                {
                    quality = params[i + 1];
                    break;
                }
            }

            jpeg_encoder_v4l_cix e;
            int ret = e.init(img.cols, img.rows, c, quality);
            if (ret == 0)
            {
                ret = e.encode(img.data, buf);
                if (ret == 0)
                {
                    e.deinit();
                    return true;
                }
            }

            // fallback to stb_image_write
        }
#endif
    }

    // bgr to rgb
    if (c == 3)
    {
        Mat img2;
        cvtColor(img, img2, COLOR_BGR2RGB);
        img = img2;
    }
    if (c == 4)
    {
        Mat img2;
        cvtColor(img, img2, COLOR_BGRA2RGBA);
        img = img2;
    }

    if (!img.isContinuous())
    {
        img = img.clone();
    }

    bool success = false;

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG")
    {
        int quality = 95;
        for (size_t i = 0; i < params.size(); i += 2)
        {
            if (params[i] == IMWRITE_JPEG_QUALITY)
            {
                quality = params[i + 1];
                break;
            }
        }

        success = stbi_write_jpg_to_func(imencode_write_func, (void*)&buf, img.cols, img.rows, c, img.data, quality);
    }
    else if (ext == ".png" || ext == ".PNG")
    {
        success = stbi_write_png_to_func(imencode_write_func, (void*)&buf, img.cols, img.rows, c, img.data, 0);
    }
    else if (ext == ".bmp" || ext == ".BMP")
    {
        success = stbi_write_bmp_to_func(imencode_write_func, (void*)&buf, img.cols, img.rows, c, img.data);
    }
    else
    {
        // unknown extension type
        return false;
    }

    return success;
}

void imshow(const String& winname, InputArray mat)
{
#if _WIN32
    std::vector<uchar> buf;
    bool result = cv::imencode(".bmp", mat, buf);
    if (result) {
        BitmapWindow::show(winname.c_str(), buf.data());
        return;
    }
    return ;
#elif __linux__ && !__ANDROID__
    if (winname == "fb")
    {
        static display_fb dpy;
        if (dpy.open() == 0)
        {
            const int dpy_w = dpy.get_width();
            const int dpy_h = dpy.get_height();

            Mat img = mat.getMat();

            // bgra to bgr
            if (img.type() == CV_8UC4)
            {
                Mat img2;
                cvtColor(img, img2, COLOR_BGRA2BGR);
                img = img2;
            }

            // resize and add border
            const int img_w = img.cols;
            const int img_h = img.rows;
            if (img_w != dpy_w || img_h != dpy_h)
            {
                Mat img2;
                if (img.type() == CV_8UC1)
                {
                    img2.create(dpy_h, dpy_w, CV_8UC1);
                    img2 = cv::Scalar(0);
                }
                if (img.type() == CV_8UC3)
                {
                    img2.create(dpy_h, dpy_w, CV_8UC3);
                    img2 = cv::Scalar(0, 0, 0);
                }

                if (img_w * dpy_h > dpy_w * img_h)
                {
                    const int img2_h = dpy_w * img_h / img_w;
                    cv::resize(img, img2(cv::Rect(0, (dpy_h - img2_h) / 2, dpy_w, img2_h)), cv::Size(dpy_w, img2_h));
                }
                else
                {
                    const int img2_w = dpy_h * img_w / img_h;
                    cv::resize(img, img2(cv::Rect((dpy_w - img2_w) / 2, 0, img2_w, dpy_h)), cv::Size(img2_w, dpy_h));
                }

                img = img2;
            }

            if (img.type() == CV_8UC1)
            {
                dpy.show_gray(img.data, img.cols, img.rows);
            }
            if (img.type() == CV_8UC3)
            {
                dpy.show_bgr(img.data, img.cols, img.rows);
            }
        }
    }
    else
#endif
    {
        fprintf(stderr, "imshow save image to %s.png\n", winname.c_str());
        imwrite(winname + ".png", mat);
    }
}

int waitKey(int delay)
{
#ifdef _WIN32
    return BitmapWindow::waitKey(delay);
#else
    (void)delay;
    fprintf(stderr, "waitKey stub\n");
    return -1;
#endif
}

} // namespace cv
