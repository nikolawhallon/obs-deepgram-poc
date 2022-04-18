Pre-requisites:
* Homebrew (https://brew.sh)

Install dependencies:

```
xcode-select --install
brew install openssl
brew install boost
```

Checkout the OBS Studio repo, checkout a commit which is known to work, and init submodules:

```
git clone git@github.com:obsproject/obs-studio.git
cd obs-studio
git checkout 7b8793f6c3dec005cd537454f8521ec3ce3d1d60
git submodule update --init --recursive
```

The reason for checking out the particular commit is that the master branch is occasionally broken.

Go to the plugins directory:

```
cd plugins
```

Checkout the `obs-deepgram-poc` repo and init its submodules:

```
git clone git@github.com:nikoladg/obs-deepgram-poc.git
cd obs-deepgram-poc
git submodule update --init --recursive
```

Go back to the plugins directory and edit the `CMakeLists.txt` file,
adding the following line to the bottom of the file:

```
cd ..
add_subdirectory(obs-deepgram-poc)
```

Build by going to the root `obs-studio` directory and running:

```
CI/build-macos.sh --bundle
```

On subsequent builds, from the `obs-studio` directory,
you may need to remove the following directory before (re)building:

```
rm -rf ../obs-build-dependencies
```