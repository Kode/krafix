FROM alpine
RUN apk add build-base git linux-headers
ADD . /krafix
RUN git clone --depth 1 https://github.com/Kode/KincTools_linux_x64.git
WORKDIR "/krafix"
RUN /KincTools_linux_x64/kmake --compile
CMD cp /krafix/build/Release/krafix /workdir/krafix_linux_x64
