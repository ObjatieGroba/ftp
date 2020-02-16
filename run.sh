#!/bin/bash

if [[ "$HW1_MODE" = "server" ]]; then
  ./server
elif [[ "$HW1_MODE" = "server" ]]; then
  ./tests
else
  echo "No such mode"
fi
