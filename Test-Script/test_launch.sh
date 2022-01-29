#!/bin/bash

CONFLICT_TYPE='ML'
CONFLICT_DATASET_FILE='conflict_dataset.csv'
CONFLICT_INDEX_FILE='conflict_index.json'
PERFORMANCE_FILE='performance.txt'
REGEX_REPLACE_INDEX_FILE='regex_replace_index.json'
REGEX_REPLACE_RESULT_FILE='regex_replace_result.txt'
REGEX_REPLACE_TREE_INDEX_DIR='regex_replace_tree_index'

for file in ../Data/$CONFLICT_TYPE/*; do

  if [ -f "$file" ]; then
    f=${file##*/}
    filename="${f%.*}"
    ./almost-rerere "$file"

    result_dir=".git/rr-cache/results/$filename"
    mkdir -p "$result_dir"

    mv ".git/rr-cache/$CONFLICT_DATASET_FILE" "$result_dir/$CONFLICT_DATASET_FILE"
    mv ".git/rr-cache/$CONFLICT_INDEX_FILE" "$result_dir/$CONFLICT_INDEX_FILE"
    mv ".git/rr-cache/$PERFORMANCE_FILE" "$result_dir/$PERFORMANCE_FILE"
    mv ".git/rr-cache/$REGEX_REPLACE_INDEX_FILE" "$result_dir/$REGEX_REPLACE_INDEX_FILE"
    mv ".git/rr-cache/$REGEX_REPLACE_RESULT_FILE" "$result_dir/$REGEX_REPLACE_RESULT_FILE"

    rm -r -f .git/rr-cache/$REGEX_REPLACE_TREE_INDEX_DIR/*

    mkdir -p "../Data/$CONFLICT_TYPE/done/"
    mv "$file" "../Data/$CONFLICT_TYPE/done/$f"
  else
    echo "Not a valid file: $file"
  fi

done
