#ifndef __LT9611_H__
#define __LT9611_H__

struct drm_display_mode default_mode_1080p = {
	/* VIC 16 */
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008, 2052,
			2200, 0, 1080, 1084, 1089, 1125, 0,
			DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)
};

struct drm_display_mode default_mode_1440p = {
 	DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 241700, 2560, 2608, 2640,
 			2720, 0, 1440, 1443, 1448, 1481, 0,
 			DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)
};

struct drm_display_mode default_mode_4k30 = {
 	DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
		   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
};

struct drm_display_mode default_mode_720p = {
 	DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC)
};

#endif //__LT9611_H__