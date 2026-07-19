#!/bin/sh

# Configure a directly executed SDK binary with the runtime extensions that
# Flatpak would normally mount below /usr. This file is sourced by test runners.
pinpoint_configure_sdk_host_environment ()
{
  pinpoint_sdk_location=$1
  pinpoint_sdk_libdir=$2
  pinpoint_codecs_version=$(awk '
    /^\[Extension org.freedesktop.Platform.codecs-extra\]$/ { in_codecs = 1; next }
    /^\[/ { in_codecs = 0 }
    in_codecs && /^version = / { sub (/^version = /, ""); print; exit }
  ' "$pinpoint_sdk_location/metadata")
  pinpoint_codecs_libdir=
  if [ -n "$pinpoint_codecs_version" ]
  then
    pinpoint_codecs_ref="org.freedesktop.Platform.codecs-extra//$pinpoint_codecs_version"
    if pinpoint_codecs_location=$(flatpak info --user --show-location \
         "$pinpoint_codecs_ref" 2>/dev/null)
    then
      :
    elif pinpoint_codecs_location=$(flatpak info --system --show-location \
         "$pinpoint_codecs_ref" 2>/dev/null)
    then
      :
    else
      echo "The SDK-declared automatic codec extension $pinpoint_codecs_ref is not installed" >&2
      return 1
    fi
    pinpoint_codecs_libdir="$pinpoint_codecs_location/files/lib"
  fi
  pinpoint_gl_versions=$(awk '
    /^\[Extension org.freedesktop.Platform.GL\]$/ { in_gl = 1; next }
    /^\[/ { in_gl = 0 }
    in_gl && /^versions = / { sub (/^versions = /, ""); print; exit }
  ' "$pinpoint_sdk_location/metadata")
  pinpoint_gl_version=${pinpoint_gl_versions%%;*}
  pinpoint_gl_ld_path=
  pinpoint_egl_manifests=
  pinpoint_dri_path=
  pinpoint_gbm_path=
  pinpoint_vulkan_manifests=

  if [ -z "$pinpoint_gl_version" ]
  then
    echo "The pinned SDK does not declare a Flatpak GL extension" >&2
    return 1
  fi

  for pinpoint_gl_driver in $(flatpak --gl-drivers)
  do
    if [ "$pinpoint_gl_driver" = host ]
    then
      continue
    fi
    pinpoint_gl_ref="org.freedesktop.Platform.GL.$pinpoint_gl_driver//$pinpoint_gl_version"
    if pinpoint_gl_location=$(flatpak info --user --show-location \
         "$pinpoint_gl_ref" 2>/dev/null)
    then
      :
    elif pinpoint_gl_location=$(flatpak info --system --show-location \
           "$pinpoint_gl_ref" 2>/dev/null)
    then
      :
    else
      continue
    fi

    pinpoint_gl_libdir="$pinpoint_gl_location/files/lib"
    pinpoint_gl_ld_path="$pinpoint_gl_ld_path${pinpoint_gl_ld_path:+:}$pinpoint_gl_libdir"
    if [ -d "$pinpoint_gl_libdir/dri" ]
    then
      pinpoint_dri_path="$pinpoint_dri_path${pinpoint_dri_path:+:}$pinpoint_gl_libdir/dri"
    fi
    if [ -d "$pinpoint_gl_libdir/gbm" ]
    then
      pinpoint_gbm_path="$pinpoint_gbm_path${pinpoint_gbm_path:+:}$pinpoint_gl_libdir/gbm"
    fi
    for pinpoint_manifest in \
      "$pinpoint_gl_location"/files/share/glvnd/egl_vendor.d/*.json
    do
      if [ -f "$pinpoint_manifest" ]
      then
        pinpoint_egl_manifests="$pinpoint_egl_manifests${pinpoint_egl_manifests:+:}$pinpoint_manifest"
      fi
    done
    for pinpoint_manifest in \
      "$pinpoint_gl_libdir"/vulkan/icd.d/*.json \
      "$pinpoint_gl_location"/files/share/vulkan/icd.d/*.json
    do
      if [ -f "$pinpoint_manifest" ]
      then
        pinpoint_vulkan_manifests="$pinpoint_vulkan_manifests${pinpoint_vulkan_manifests:+:}$pinpoint_manifest"
      fi
    done
  done

  if [ -z "$pinpoint_gl_ld_path" ] || \
     [ -z "$pinpoint_egl_manifests" ] || \
     [ -z "$pinpoint_dri_path" ]
  then
    echo "No complete Flatpak GL driver extension is installed for SDK graphics version $pinpoint_gl_version" >&2
    return 1
  fi

  export LD_LIBRARY_PATH="${pinpoint_codecs_libdir:+$pinpoint_codecs_libdir:}$pinpoint_gl_ld_path:$pinpoint_sdk_libdir:$pinpoint_sdk_libdir/pulseaudio${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export __EGL_VENDOR_LIBRARY_FILENAMES="$pinpoint_egl_manifests"
  export LIBGL_DRIVERS_PATH="$pinpoint_dri_path"
  if [ -n "$pinpoint_gbm_path" ]
  then
    export GBM_BACKENDS_PATH="$pinpoint_gbm_path"
  fi
  if [ -n "$pinpoint_vulkan_manifests" ]
  then
    export VK_DRIVER_FILES="$pinpoint_vulkan_manifests"
  fi
  export GST_PLUGIN_SCANNER="$pinpoint_sdk_libdir/gstreamer-1.0/gst-plugin-scanner"
  export GST_PLUGIN_SYSTEM_PATH_1_0="${pinpoint_codecs_libdir:+$pinpoint_codecs_libdir/gstreamer-1.0:}$pinpoint_sdk_libdir/gstreamer-1.0"
}
