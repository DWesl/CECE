#!/bin/bash
mkdir -p data
./download_hemco_data.py HEMCO/MACCITY/v2014-07/MACCity_4x5.nc -o data/$(basename HEMCO/MACCITY/v2014-07/MACCity_4x5.nc)
