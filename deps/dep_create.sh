#!/bin/bash

#clear env
unalias -a

PWD="$(cd $(dirname $0); pwd)"

OS_ARCH="$(uname -m)" || exit 1
OS_RELEASE="0"

if [[ ! -f /etc/os-release ]]; then
  echo "[ERROR] os release info not found" 1>&2 && exit 1
fi

source /etc/os-release || exit 1

PNAME=${PRETTY_NAME:-"${NAME} ${VERSION}"}
PNAME="${PNAME} (${OS_ARCH})"

function compat_centos9() {
  echo_log "[NOTICE] '$PNAME' is compatible with CentOS 9, use el9 dependencies list"
  OS_RELEASE=9
}

function compat_centos8() {
  echo_log "[NOTICE] '$PNAME' is compatible with CentOS 8, use el8 dependencies list"
  OS_RELEASE=8
}

function compat_centos7() {
  echo_log "[NOTICE] '$PNAME' is compatible with CentOS 7, use el7 dependencies list"
  OS_RELEASE=7
}

function not_supported() {
  echo_log "[ERROR] '$PNAME' is not supported yet."
}

function version_ge() {
  test "$(awk -v v1=$VERSION_ID -v v2=$1 'BEGIN{print(v1>=v2)?"1":"0"}' 2>/dev/null)" == "1"
}

function echo_log() {
  echo -e "[dep_create.sh] $@"
}

function echo_err() {
  echo -e "[dep_create.sh][ERROR] $@" 1>&2
}

function get_os_release() {
  if [[ "${OS_ARCH}x" == "x86_64x" ]]; then
    case "$ID" in
      alinux)
        version_ge "2.1903" && compat_centos7 && return
        ;;
      alios)
        version_ge "7.2" && compat_centos7 && return
        ;;
      anolis)
        version_ge "7.0" && compat_centos7 && return
        ;;
      ubuntu)
        version_ge "16.04" && compat_centos7 && return
        ;;
      centos)
        version_ge "7.0" && OS_RELEASE=7 && return
        ;;
      almalinux)
        version_ge "8.0" && compat_centos8 && return
        ;;
      debian)
        version_ge "9" && compat_centos7 && return
        ;;
      fedora)
        version_ge "33" && compat_centos7 && return
        ;;
      openEuler)
        version_ge "22" && compat_centos9 && return
        ;;
      opensuse-leap)
        version_ge "15" && compat_centos7 && return
        ;;
      #suse
      sles)
        version_ge "15" && compat_centos7 && return
        ;;
      uos)
        version_ge "20" && compat_centos7 && return
        ;;
    esac
  elif [[ "${OS_ARCH}x" == "aarch64x" ]]; then
    case "$ID" in
      alios)
        version_ge "7.0" && compat_centos7 && return
        ;;
      anolis)
        version_ge "7.0" && compat_centos7 && return
        ;;
      centos)
        version_ge "7.0" && OS_RELEASE=7 && return
        ;;
      debian)
        version_ge "9" && compat_centos7 && return
        ;;
      openEuler)
        version_ge "22" && compat_centos9 && return
        ;;
      ubuntu)
        version_ge "16.04" && compat_centos7 && return
        ;;
    esac
  elif [[ "${OS_ARCH}x" == "sw_64x" ]]; then
    case "$ID" in
      UOS)
        version_ge "20" && OS_RELEASE=20 && return
      ;;
    esac
  fi
  not_supported && return 1
}

get_os_release || exit 1

OS_TAG="el$OS_RELEASE.$OS_ARCH"
DEP_FILE="obstack.${OS_TAG}.deps"

echo -e "check dependencies profile for ${OS_TAG}... \c"
if [[ ! -f "${DEP_FILE}" ]]; then
    echo "NOT FOUND" 1>&2
    exit 2
else
    echo "FOUND"
fi

mkdir "${PWD}/pkg" >/dev/null 2>&1

echo -e "check repository address in profile... \c"
REPO="$(grep -Eo "repo=.*" "${DEP_FILE}" | awk -F '=' '{ print $2 }' 2>/dev/null)"
if [[ $? -eq 0 ]]; then
    echo "$REPO"
else
    echo "NOT FOUND" 1>&2
    exit 3
fi

echo "download dependencies..."
RPMS="$(grep '\.rpm' "${DEP_FILE}" | grep -v '^#')"

for pkg in $RPMS
do
  if [[ -f "${PWD}/pkg/${pkg}" ]]; then
    echo "find package <${pkg}> in cache"
  else
    echo -e "download package <${pkg}>... \c"
    TEMP=$(mktemp -p "/" -u ".${pkg}.XXXX")
    DOWNLOAD_URL="${REPO}/${pkg}"
    echo $REPO
    wget "$DOWNLOAD_URL" -q -O "${PWD}/pkg/${TEMP}"
    if [[ $? -eq 0 ]]; then
      mv -f "${PWD}/pkg/$TEMP" "${PWD}/pkg/${pkg}"
      echo "SUCCESS"
    else
      rm -rf "${PWD}/pkg/$TEMP"
      echo "FAILED" 1>&2
      exit 4
    fi
  fi
  echo -e "unpack package <${pkg}>... \c"
  rpm2cpio "${PWD}/pkg/${pkg}" | cpio -di -u --quiet

  if [[ $? -eq 0 ]]; then
    echo "SUCCESS"
  else
    echo "FAILED" 1>&2
    exit 5
  fi
done
