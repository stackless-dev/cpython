#!/bin/sh

# Usage: $0 git_python_dir version

# Create a source archive below $TMPDIR, that has correct internal
# timestamps and contains its members in the correct order.
# This way make doesn't try to recreate generated files.

if [ $# != 3 ] ; then
   echo "Usage:" 1>&2
   echo "  $0 git_python_dir -v version" 1>&2
   echo "  $0 git_python_dir -r revision" 1>&2
   echo "  version is: major '.' minor '.' micro" 1>&2
   exit 1
fi

: ${excludes:=.gitignore .git* .hg* .bzrignore .mention-bot .travis.yml .readthedocs.yml .azure-pipelines}
 
git_python_dir="$1" ; shift
if [ "x$1" = "x-v" ] ; then
  shift
  version="$1" ; shift
  tag="v${version}-slp"
  srcdir="stackless-$(echo ${version} | tr -d '.')-export"
elif [ "x$1" = "x-r" ] ; then
  shift
  version="$1" ; shift
  tag="${version}"
  srcdir="stackless-python-${version}"
else
  echo "Invalid option '$1'." 1>&2
  exit 1
fi

set -x
set -e   # break on errors
set -f   # no globbing

tmpdir="${TMPDIR:-/tmp}/$tag.$$"
mkdir "$tmpdir"

cd "$git_python_dir"
git archive --format=tar --prefix="${srcdir}/" "$tag" | \
  ( cd "${tmpdir}" && tar xf - )
cd "${tmpdir}"
( cd "${srcdir}" && set +f && rm -rf $excludes )

touch "${srcdir}/Include"
touch "${srcdir}/Python"
touch "${srcdir}/Include/Python-ast.h"
touch "${srcdir}/Python/Python-ast.c"

tar -cJf "${srcdir}.tar.xz" --numeric-owner --owner=0 --group=0  $(ls -rtd $(find "${srcdir}" -mindepth 1 -maxdepth 1))

echo "Created archive ${tmpdir}/${srcdir}.tar.xz"
