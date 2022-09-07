#!/bin/bash
# dump out the values and metadata for the system:/config tree in libelektra

for key in `kdb ls system:/config`
do
  echo -n "[Key] ${key}"
  echo -n "  [Value] \"$(kdb get "${key}")\""
  echo -n "  [Metadata] " `kdb meta-show "${key}"`
  echo
done
