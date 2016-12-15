DIR=$(dirname $0)

BASE_URL=https://github.com/Pulse-Eight/platform/trunk/src
svn export --force $BASE_URL $DIR/src

