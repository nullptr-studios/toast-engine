```bash
./refgen.exe --database ${CACHED_DIR} --output "engine/generated" --input "folder/to/search/1"
```

`--database` is the path where the json metadata will be saved

`--output` is the path where the generated files will be stored 

`--input` is the path where the ToastNodes are stored, the program will search for all
the `.hpp` files containing `[[ToastNode]]`
