#!/bin/bash

HW1_HOST="$(dig +short $HW1_HOST | tail -n 1)"

if [[ "$HW1_MODE" = "server" ]]; then
  ./server
elif [[ "$HW1_MODE" = "tests" ]]; then
  ./tests
else
  echo "No such mode"
fi
