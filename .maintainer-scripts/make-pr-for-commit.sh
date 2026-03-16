#/bin/bash

# SPDX-FileCopyrightText: Copyright (c) Marcus Holland-Moritz
# SPDX-License-Identifier: MIT

set -e

commit=$1

# Record the name of the current branch so we can switch back to it later
current_branch=$(git branch --show-current)

target_branch="merge/target-$commit"
source_branch="merge/source-$commit"

# Create a new branch `merge/target-$commit` that points *before* the commit
git checkout -b "$target_branch" "$commit^"

# Create a new branch `merge/source-$commit` that points to the commit
git checkout -b "$source_branch" "$commit"

# Push both branches to the remote repository
git push origin "$target_branch:$target_branch"
git push origin "$source_branch:$source_branch"

# Create a pull request from `merge/source-$commit` to `merge/target-$commit`
gh pr create --base "$target_branch" --head "$source_branch" --title "PR for commit $commit" --body "This PR is automatically generated to create a pull request for commit $commit."

# Switch back to the original branch
git checkout "$current_branch"

# Cleanup: delete the temporary branches locally
git branch -D "$target_branch"
git branch -D "$source_branch"
