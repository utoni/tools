#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdint.h>
#include <inttypes.h>

#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xutil.h>

#include <assert.h>


static uint64_t calcChkSum(XImage* ximg, int width, int height)
{
  assert(ximg);

  uint64_t chksum = 0;
  unsigned long red_mask = ximg->red_mask;
  unsigned long green_mask = ximg->green_mask;
  unsigned long blue_mask = ximg->blue_mask;

  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      unsigned long pixel = XGetPixel(ximg, x, y);

      unsigned int blue = pixel & blue_mask;   /* 0x0000FF */
      unsigned int green = pixel & green_mask; /* 0x00FF00 */
      unsigned int red = pixel & red_mask;     /* 0xFF0000 */
      unsigned int rgb_chk = (red | green | blue);

      chksum = (chksum << 24) | ( (chksum >> 40) ^ (rgb_chk & 0xFFFFFF) );
    }
  }
  return chksum;
}

static ssize_t calcImgDiff(XImage* xnew, XImage* xold, int width, int height, unsigned int maxdiff)
{
  assert(xnew || xold);

  unsigned int diff = 0;
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      unsigned long newpixel = XGetPixel(xnew, x, y);
      unsigned long oldpixel = XGetPixel(xold, x, y);
      if (newpixel != oldpixel)
        diff++;
      if (diff >= maxdiff)
        return diff;
    }
  }

  return diff;
}

static int xinit(char* disp_name, Display** disp_ptr, Window* root_ptr, int* width_ptr, int* height_ptr)
{
  assert(disp_ptr || root_ptr || width_ptr || height_ptr);

  *disp_ptr = XOpenDisplay(disp_name);
  if (! *disp_ptr)
    return 1;
  *root_ptr = DefaultRootWindow(*disp_ptr);
  XWindowAttributes attr;
  if (XGetWindowAttributes(*disp_ptr, *root_ptr, &attr) == 0)
    return 1;
  *width_ptr = attr.width;
  *height_ptr = attr.height;
  return 0;
}

static int genChkSum = 0;
static long long maxIter = -1;
static useconds_t sleep_interval = 1 * 1000 * 1000;
static unsigned int maxdiff = 1000;
static unsigned int bounds[4];

static void parseArgs(int argc, char** argv)
{
  int opt;

  while ((opt = getopt(argc, argv, "cd:m:i:x:y:w:h:")) != -1) {
    switch (opt) {
      case 'c':
        genChkSum = 1;
        break;
      case 'd':
        maxdiff = (unsigned int)atoi(optarg);
        break;
      case 'm':
        maxIter = atoll(optarg);
        break;
      case 'i':
        sleep_interval = atoll(optarg) * 1000;
        break;
      case 'x':
        bounds[0] = (unsigned int)atoi(optarg);
        break;
      case 'y':
        bounds[1] = (unsigned int)atoi(optarg);
        break;
      case 'w':
        bounds[2] = (unsigned int)atoi(optarg);
        break;
      case 'h':
        bounds[3] = (unsigned int)atoi(optarg);
        break;
      default:
        fprintf(stderr, "usage: %s [-c] [-d maxDiff] [-m maxIter] [-i sleepTime in ms] [-x n] [-y n] [-w n] [-h n]\n", argv[0]);
        exit(1);
    }
  }
}

static int doLoopOnce(Display* disp, Window* root, XImage** old)
{
  assert(disp || root || old);

  XImage* image = XGetImage(disp, *root, bounds[0], bounds[1], bounds[2], bounds[3], AllPlanes, ZPixmap);
  if (!image)
    return 1;

  if (genChkSum != 0) {
    printf("ChkSum: %#" PRIx64 "\n", calcChkSum(image, bounds[2], bounds[3]));
  }

  printf("ImgDif: %llu\n", (long long unsigned int)calcImgDiff(image, *old, bounds[2], bounds[3], maxdiff));

  XDestroyImage(*old);
  *old = image;
  usleep(sleep_interval);
  return 0;
}

static void checkBound(unsigned int value, unsigned int max, const char* name)
{
  if (value > max) {
    printf("Bound check failed for %s: %u > %u\n", name, value, max);
    abort();
  }
}

int main(int argc, char** argv)
{
  Display* disp;
  Window root;
  int width, height;

  if (xinit(NULL, &disp, &root, &width, &height)) {
    fprintf(stderr, "%s: xinit failed\n", argv[0]);
    abort();
  }
  printf("X Resolution: %ux%u\n", width, height);

  bounds[0] = 0;
  bounds[1] = 0;
  bounds[2] = width;
  bounds[3] = height;
  parseArgs(argc, argv);
  checkBound(bounds[0], width-1, "-x");
  checkBound(bounds[1], height-1, "-y");
  checkBound(bounds[2], width-bounds[0], "-w");
  checkBound(bounds[3], height-bounds[1], "-h");
  printf("X Image Bounds (X,Y,W,H): %u, %u, %u, %u\n", bounds[0], bounds[1], bounds[2], bounds[3]);

  XImage* old = XGetImage(disp, root, bounds[0], bounds[1], bounds[2], bounds[3], AllPlanes, ZPixmap);
  if (!old) {
    fprintf(stderr, "%s: XGetImage failed\n", argv[0]);
    abort();
  }

  if (maxIter < 0) {
    while (1) {
      if (doLoopOnce(disp, &root, &old) != 0)
        break;
    }
  } else {
    for (long long i = 0; i < maxIter; ++i) {
      if (doLoopOnce(disp, &root, &old) != 0)
        break;
    }
  }

  return 0;
}
