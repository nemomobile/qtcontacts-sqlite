Name: qtcontacts-sqlite-qt5
Version: 0.0.12
Release: 0
Summary: SQLite-based plugin for QtPIM Contacts
Group: System/Plugins
License: TBD
URL: TBD
Source0: %{name}-%{version}.tar.gz
Requires: pkgconfig(Qt5Core)
Requires: pkgconfig(Qt5Sql)
Requires: pkgconfig(Qt5DBus)
Requires: pkgconfig(Qt5Contacts)

%description
%{summary}.

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/plugins/contacts/*.so*

%package tests
Summary:    Unit tests for qtcontacts-sqlite-qt5
Group:      System/Libraries
Requires:  pkgconfig(Qt5Test)
Requires:   %{name} = %{version}-%{release}

%description tests
This package contains unit tests for the qtcontacts-sqlite-qt5 library.

%files tests
%defattr(-,root,root,-)
/opt/tests/qtcontacts-sqlite-qt5/*


%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%qmake5_install

