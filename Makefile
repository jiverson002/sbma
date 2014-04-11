default:
	@if [ ! -d lib ] ; then mkdir lib; fi
	@if [ ! -d bin ] ; then mkdir bin; fi
	make -C src

clean:
	make -C src clean

distclean:
	make -C src distclean
