/*A REXX program that demonstrates the use of rxttf_image */

call RxFuncAdd 'rxttf_image', 'RXTTF', 'rxttf_image'


message='Hello!'

/* replace d:\psfonts\cour.ttf with a ttf font filename on your system */
ttf_file='e:\os2\mdos\winos2\system\times.ttf'
rc = rxttf_image(message, ttf_file, 14,'data')

if rc > 0 then do
  say 'Failed with rc =' rc
  exit
end

say data.!rows 'rows x' data.!cols 'cols'

do i = 0 to data.!rows - 1
  say translate(data.i, ' *', XRANGE('00'x, '01'x))
end


