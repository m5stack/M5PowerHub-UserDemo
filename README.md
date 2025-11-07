# M5PowerHub UserDemo

## Build

### Fetch Dependencies

```bash
python ./fetch_repos.py
```

## IDF Build

#### Tool Chains

[ESP-IDF v5.4.1](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/index.html)

#### Build

```bash
cd powerhub
```

```bash
idf.py build
```

#### Flash

```bash
idf.py -p <YourPort> flash -b 1500000
```