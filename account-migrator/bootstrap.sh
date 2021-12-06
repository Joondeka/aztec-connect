#!/bin/bash
set -e

LINK_FOLDER="--link-folder `pwd`/../.yarn"

yarn install
yarn link $LINK_FOLDER @aztec/barretenberg
yarn build