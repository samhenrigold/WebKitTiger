#!/bin/sh

# Get the OpenGL source path
if [ -z $OPENGL_SOURCE_PATH ]; then
  exit 1
else
  openglsourcepath=$OPENGL_SOURCE_PATH
fi

# Begin processing and extracting public parts of the required source files
currentpath=`pwd`

cd $openglsourcepath/GLHeaders

# Extract the public parts of the vertex program source files
awk '/BEGINPUBLIC/ { i = 1; next } /ENDPUBLIC/ { i = 0; next } { if (i == 1) print $0 }' gldLFSStream.h | \
sed -e 's/gleCalloc/calloc/g' -e 's/gleFree/free/g' -e 's/GlobalHeader/LFSGlobalHeader/g' -e 's/gleMalloc/malloc/g' \
    -e 's/glePrintErr0/printf/g' > $currentpath/LFSConstants.h
echo "GLHeaders/gldLFSStream.h -> LFSConstants.h done."

cd $openglsourcepath/GLEngine/gle

awk '/BEGINPUBLIC/ { i = 1; next } /ENDPUBLIC/ { i = 0; next } { if (i == 1) print $0 }' gle_lfs_parse.h | \
sed -e 's/gleCalloc/calloc/g' -e 's/gleFree/free/g' -e 's/GlobalHeader/LFSGlobalHeader/g' -e 's/gleMalloc/malloc/g' \
    -e 's/glePrintErr0/printf/g' > $currentpath/LFSParse.h
echo "GLEngine/gle/gle_lfs_parse.h -> LFSParse.h done."

echo "#include \"LFSGlobalHeader.h\"" > $currentpath/LFSParse.c
awk '/BEGINPUBLIC/ { i = 1; next } /ENDPUBLIC/ { i = 0; next } { if (i == 1) print $0 }' gle_lfs_parse.c | \
sed -e 's/gleCalloc/calloc/g' -e 's/gleFree/free/g' -e 's/GlobalHeader/LFSGlobalHeader/g' -e 's/gleMalloc/malloc/g' \
    -e 's/glePrintErr0/printf/g' >> $currentpath/LFSParse.c
echo "GLEngine/gle/gle_lfs_parse.c -> LFSParse.c done."

awk '/BEGINPUBLIC/ { i = 1; next } /ENDPUBLIC/ { i = 0; next } { if (i == 1) print $0 }' gle_lfs_stream.h | \
sed -e 's/gleCalloc/calloc/g' -e 's/gleFree/free/g' -e 's/GlobalHeader/LFSGlobalHeader/g' -e 's/gleMalloc/malloc/g' \
    -e 's/glePrintErr0/printf/g' > $currentpath/LFSStream.h
echo "GLEngine/gle/gle_lfs_stream.h -> LFSStream.h done."

echo "#include \"LFSGlobalHeader.h\"" > $currentpath/LFSStream.c
awk '/BEGINPUBLIC/ { i = 1; next } /ENDPUBLIC/ { i = 0; next } { if (i == 1) print $0 }' gle_lfs_stream.c | \
sed -e 's/gleCalloc/calloc/g' -e 's/gleFree/free/g' -e 's/GlobalHeader/LFSGlobalHeader/g' -e 's/gleMalloc/malloc/g' \
    -e 's/glePrintErr0/printf/g' >> $currentpath/LFSStream.c
echo "GLEngine/gle/gle_lfs_stream.c -> LFSStream.c done."
