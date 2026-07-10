#!/bin/sh
# Builds MindfulCompute with a timestamped build number (shown in the
# menu and on the start panel), so a stale build is easy to spot.
set -e
cd "$(dirname "$0")"

xcodegen generate
xcodebuild -project MindfulCompute.xcodeproj -scheme MindfulCompute \
  -configuration Release -derivedDataPath build \
  CURRENT_PROJECT_VERSION="$(date +%y%m%d.%H%M)" build

echo ""
echo "Built: build/Build/Products/Release/MindfulCompute.app"
/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" \
  build/Build/Products/Release/MindfulCompute.app/Contents/Info.plist

if [ "$1" = "--install" ]; then
  pkill -f MindfulCompute.app || true
  rm -rf /Applications/MindfulCompute.app
  cp -R build/Build/Products/Release/MindfulCompute.app /Applications/
  open /Applications/MindfulCompute.app
  echo "Installed to /Applications and launched."
fi
