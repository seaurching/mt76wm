#!/bin/sh
# gen_project.sh - generate mtkwm.xcodeproj via xcodegen and patch the
# product type to com.apple.product-type.kernel-extension (XcodeGen has no
# native kext target type, so we generate a bundle and rewrite it).
set -e
cd "$(dirname "$0")/.."
xcodegen generate
P=mtkwm.xcodeproj/project.pbxproj
sed -i '' \
    -e 's/com\.apple\.product-type\.bundle/com.apple.product-type.kernel-extension/g' \
    -e 's/wrapper\.cfbundle/wrapper.kernel-extension/g' \
    "$P"
echo "patched $P -> kernel-extension product type"
