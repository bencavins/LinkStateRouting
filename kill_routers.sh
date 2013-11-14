#!/bin/bash

for PID in $(pidof routed_LS)
do
    kill $PID
done