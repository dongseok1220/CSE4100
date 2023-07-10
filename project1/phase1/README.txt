[system programming lecture]

-project 1 baseline

csapp.{c,h}
        CS:APP3e functions

shellex.c
        Simple shell example

20191619 이동석

1. 컴파일
make 명령어를 입력하면 myshell 실행파일이 생성된다. 

2. myshell 프로그램에서는 다음과 같은 동작이 가능하며 실행시 히스토리 로그를 저장할 파일이 홈 디렉토리에 생성된다. 
cd : 쉘에 존재하는 디렉터리 탐색
history : 사용자가 입력한 명령어 출력
!! / !# : 가장 최근에 입력한 명령어 / #번호에 해당하는 명령어 실행 혹은 대치
ls : 디렉터리 내용물 출력
mkdir : 디렉터리 생성
rmdir : 디렉터리 삭제
touch : 파일 생성
cat : 파일 내용 읽기
echo : 파일 내용 출력
exit : 쉘 종료
quit : 쉘 종료


