Name:       sailfish-nfcshare
Version:    1.0.4
Release:    1
License:    BSD
Summary:    NFC share plugin
URL:        https://github.com/sailfishos/sailfish-nfcshare
Source0:    %{name}-%{version}.tar.gz

Requires: sailfishshare-components
Requires: nfcd >= 1.2

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(libnfcdef)
BuildRequires:  pkgconfig(nemotransferengine-qt5) >= 2
BuildRequires:  qt5-qttools
BuildRequires:  qt5-qttools-linguist
BuildRequires:  sailfish-svg2png

%define nfcshare_uidir /usr/share/nemo-transferengine/plugins/nfcshare
%define nfcshare_qmlplugindir %{_libdir}/qt5/qml/org/sailfishos/nfcshare
%define transferengine_plugindir %{_libdir}/nemo-transferengine/plugins

%if %{defined sailfishos_version} && 0%{?sailfishos_version} < 50000
%define use_svg 1
%else
%define use_svg 0
%endif

%description
%{summary}.

%package ts-devel
Summary: Translation source for %{name}

%description ts-devel
Translation source for %{name}

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 \
%if %{?use_svg}
  CONFIG+=use_svg
%endif

%make_build

%install
%qmake5_install

%files
%license LICENSE
%{_datadir}/translations/nfcshare_eng_en.qm
%{transferengine_plugindir}/sharing/libnfcshareplugin.so
%{nfcshare_qmlplugindir}
%{nfcshare_uidir}
%if %{?use_svg} == 0
%{_datadir}/themes/sailfish-default/silica/*/icons/*.png
%endif

%files ts-devel
%{_datadir}/translations/source/*.ts
