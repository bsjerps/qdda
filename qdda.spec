Summary:	The quick & dirty dedupe analyzer
Name:		qdda
Version:	2.0.9
Release:	1%{?prerel:.~%prerel}
URL:		https://github.com/outrunnl/qdda
Source0:	%{name}-%{version}.tbz2
License:	GPLv3+
Group:		Outrun/Extras

%description
Checks files or block devices for duplicate blocks to estimate deduplication
efficiency on dedupe capable storage systems, using key-value stores in SQLite,
MD5 hashing and LZ4 compression.  It also estimates compression ratios for
XtremIO X1 and X2 as well as VMAX AFA (experimental).

%prep
%setup -q -n %{name}

%build
make -j4 version=%{version}

%install
rm -rf %{buildroot}
mkdir %{buildroot}

%makeinstall

%files
%defattr(0755,root,root)
/usr/bin/qdda
%defattr(0644,root,root)
/usr/share/man/man1/qdda.1.gz
%defattr(0644,root,root,0755)
/usr/share/qdda
