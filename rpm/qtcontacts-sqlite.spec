Name: qtcontacts-sqlite
Version: 0.0.1
Release: 0
Summary: SQLite-based plugin for QtContacts
Group: System/Plugins
License: TBD
URL: TBD
Source0: %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(QtCore)
BuildRequires: pkgconfig(QtSql)
BuildRequires: pkgconfig(QtDBus)
BuildRequires: pkgconfig(QtContacts)

%description
%{summary}.

%files
%defattr(-,root,root,-)
%{_libdir}/qt4/plugins/contacts/*.so*

%package tests
Summary:    Unit tests for qtcontacts-sqlite
Group:      System/Libraries
BuildRequires:  pkgconfig(QtTest)
Requires:   %{name} = %{version}-%{release}

%description tests
This package contains unit tests for the qtcontacts-sqlite library.

%files tests
%defattr(-,root,root,-)
/opt/tests/qtcontacts-sqlite/*


%prep
%setup -q -n %{name}-%{version}

%build
%qmake
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%qmake_install

