# By default, the RPM will install to the standard REDHAWK SDR root location (/var/redhawk/sdr)
# You can override this at install time using --prefix /new/sdr/root when invoking rpm (preferred method, if you must)
%{!?_sdrroot: %global _sdrroot /var/redhawk/sdr}
%define _prefix %{_sdrroot}
Prefix:         %{_prefix}

# Point install paths to locations within our target SDR root
%define _sysconfdir    %{_prefix}/etc
%define _localstatedir %{_prefix}/var
%define _mandir        %{_prefix}/man
%define _infodir       %{_prefix}/info

Name:           rh.psd
Version:        2.0.2
Release:        1%{?dist}
Summary:        Component %{name}

Group:          REDHAWK/Components
License:        None
Source0:        %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  redhawk-devel >= 2.0
Requires:       redhawk >= 2.0

BuildRequires:  rh.dsp-devel
Requires:       rh.dsp
BuildRequires:  rh.fftlib-devel
Requires:       rh.fftlib

# Interface requirements
BuildRequires:  bulkioInterfaces >= 2.0
Requires:       bulkioInterfaces >= 2.0


%description
Component %{name}
 * Commit: __REVISION__
 * Source Date/Time: __DATETIME__


%prep
%setup -q


%build
# Implementation cpp
pushd cpp
./reconf
%define _bindir %{_prefix}/dom/components/rh/psd/cpp
%configure
make %{?_smp_mflags}
popd
# Implementation cpp_rfnoc
pushd cpp_rfnoc
./reconf
%define _bindir %{_prefix}/dom/components/rh/psd/cpp_rfnoc
%configure
make %{?_smp_mflags}
popd


%install
rm -rf $RPM_BUILD_ROOT
# Implementation cpp
pushd cpp
%define _bindir %{_prefix}/dom/components/rh/psd/cpp
make install DESTDIR=$RPM_BUILD_ROOT
popd
# Implementation cpp_rfnoc
pushd cpp_rfnoc
%define _bindir %{_prefix}/dom/components/rh/psd/cpp_rfnoc
make install DESTDIR=$RPM_BUILD_ROOT
popd


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,redhawk,redhawk,-)
%dir %{_prefix}/dom/components/rh/psd
%{_prefix}/dom/components/rh/psd/psd.scd.xml
%{_prefix}/dom/components/rh/psd/psd.prf.xml
%{_prefix}/dom/components/rh/psd/psd.spd.xml
%{_prefix}/dom/components/rh/psd/cpp
%{_prefix}/dom/components/rh/psd/cpp_rfnoc

