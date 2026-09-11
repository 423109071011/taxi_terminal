#!/bin/sh
/app/quectel-CM > /dev/null &
sleep 5
./taxi_terminal deploy/config
