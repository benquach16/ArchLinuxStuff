
while true; do
    xsetroot -name "` date`"
done &
[ -f /etc/xprofile ] && source /etc/xprofile
[ -f ~/.xprofile ] && source ~/.xprofile
#exec openbox-session
#exec dwm
#exec echinus
exec awesome

